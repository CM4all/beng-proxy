// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "RecordingHttpResponseHandler.hxx"
#include "nghttp2/Client.hxx"
#include "nghttp2/Util.hxx"
#include "fs/FilteredSocket.hxx"
#include "http/Method.hxx"
#include "istream/UnusedPtr.hxx"
#include "event/SocketEvent.hxx"
#include "event/FineTimerEvent.hxx"
#include "net/SocketPair.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"

#include <nghttp2/nghttp2.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using std::string_view_literals::operator""sv;

namespace {

static constexpr auto response_body = "hello"sv;

/**
 * A minimal HTTP/2 server which answers every request with a
 * canned response.  It exists because our own #NgHttp2::ServerConnection
 * cannot generate the response variants this test needs.
 */
class FakeServer final {
public:
	enum class Mode {
		/**
		 * "200 OK" with a body, followed by a trailer HEADERS
		 * frame.
		 */
		TRAILER,

		/**
		 * "100 Continue" followed by the final "200 OK".
		 */
		INFORMATIONAL,
	};

private:
	const UniqueSocketDescriptor socket;

	const Mode mode;

	nghttp2_session *session;

	SocketEvent event;

public:
	FakeServer(EventLoop &event_loop, UniqueSocketDescriptor &&_socket,
		   Mode _mode) noexcept
		:socket(std::move(_socket)), mode(_mode),
		 event(event_loop, BIND_THIS_METHOD(OnSocketReady), socket)
	{
		nghttp2_session_callbacks *callbacks;
		nghttp2_session_callbacks_new(&callbacks);
		nghttp2_session_callbacks_set_send_callback(callbacks, SendCallback);
		nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
								     OnFrameRecvCallback);

		nghttp2_session_server_new(&session, callbacks, this);
		nghttp2_session_callbacks_del(callbacks);

		nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, nullptr, 0);
		nghttp2_session_send(session);

		event.ScheduleRead();
	}

	~FakeServer() noexcept {
		event.Cancel();
		nghttp2_session_del(session);
	}

	FakeServer(const FakeServer &) = delete;
	FakeServer &operator=(const FakeServer &) = delete;

private:
	void SubmitResponse(int32_t stream_id) noexcept {
		switch (mode) {
		case Mode::TRAILER:
			{
				const nghttp2_nv nv[] = {
					NgHttp2::MakeNv(":status"sv, "200"sv),
				};

				nghttp2_data_provider dp{};
				dp.read_callback = ReadCallback;

				nghttp2_submit_response(session, stream_id,
							nv, std::size(nv), &dp);
			}

			break;

		case Mode::INFORMATIONAL:
			{
				const nghttp2_nv informational[] = {
					NgHttp2::MakeNv(":status"sv, "100"sv),
				};

				nghttp2_submit_headers(session, NGHTTP2_FLAG_NONE,
						       stream_id, nullptr,
						       informational,
						       std::size(informational),
						       nullptr);

				const nghttp2_nv final_[] = {
					NgHttp2::MakeNv(":status"sv, "200"sv),
					NgHttp2::MakeNv("x-final"sv, "yes"sv),
				};

				nghttp2_submit_response(session, stream_id,
							final_, std::size(final_),
							nullptr);
			}

			break;
		}
	}

	int OnFrameRecv(const nghttp2_frame &frame) noexcept {
		if (frame.hd.type == NGHTTP2_HEADERS &&
		    frame.headers.cat == NGHTTP2_HCAT_REQUEST &&
		    (frame.hd.flags & NGHTTP2_FLAG_END_STREAM) != 0)
			SubmitResponse(frame.hd.stream_id);

		return 0;
	}

	void OnSocketReady(unsigned) noexcept {
		std::byte buffer[16384];

		ssize_t nbytes;
		while ((nbytes = socket.ReadNoWait(buffer)) > 0)
			nghttp2_session_mem_recv(session,
						 (const uint8_t *)buffer,
						 nbytes);

		nghttp2_session_send(session);
	}

	static ssize_t SendCallback(nghttp2_session *, const uint8_t *data,
				    size_t length, int,
				    void *user_data) noexcept {
		auto &s = *(FakeServer *)user_data;

		auto nbytes = s.socket.WriteNoWait({(const std::byte *)data, length});
		if (nbytes < 0)
			return NGHTTP2_ERR_CALLBACK_FAILURE;

		return nbytes;
	}

	static int OnFrameRecvCallback(nghttp2_session *,
				       const nghttp2_frame *frame,
				       void *user_data) noexcept {
		auto &s = *(FakeServer *)user_data;
		return s.OnFrameRecv(*frame);
	}

	static ssize_t ReadCallback(nghttp2_session *session, int32_t stream_id,
				    uint8_t *buf, size_t length,
				    uint32_t *data_flags,
				    nghttp2_data_source *, void *) noexcept {
		if (length < response_body.size())
			return NGHTTP2_ERR_CALLBACK_FAILURE;

		std::copy(response_body.begin(), response_body.end(),
			  (char *)buf);

		/* NO_END_STREAM because the trailer HEADERS frame
		   terminates the stream */
		*data_flags = NGHTTP2_DATA_FLAG_EOF|NGHTTP2_DATA_FLAG_NO_END_STREAM;

		const nghttp2_nv nv[] = {
			NgHttp2::MakeNv("x-trailer"sv, "foo"sv),
		};

		nghttp2_submit_trailer(session, stream_id, nv, std::size(nv));

		return response_body.size();
	}
};

class MyConnectionHandler final : public NgHttp2::ConnectionHandler {
public:
	std::exception_ptr error;
	bool closed = false;

	void OnNgHttp2ConnectionError(std::exception_ptr &&e) noexcept override {
		error = std::move(e);
	}

	void OnNgHttp2ConnectionClosed() noexcept override {
		closed = true;
	}
};

/**
 * Counts the number of OnHttpResponse() invocations; invoking it more
 * than once is a bug.
 */
struct CountingHttpResponseHandler final : RecordingHttpResponseHandler {
	unsigned n_responses = 0;

	using RecordingHttpResponseHandler::RecordingHttpResponseHandler;

	void OnHttpResponse(HttpStatus _status, StringMap &&_headers,
			    UnusedIstreamPtr _body) noexcept override {
		++n_responses;
		RecordingHttpResponseHandler::OnHttpResponse(_status,
							     std::move(_headers),
							     std::move(_body));
	}
};

struct Instance : TestInstance {
	/**
	 * Break the #EventLoop if the test does not finish, so a
	 * failure does not hang the test suite.
	 */
	FineTimerEvent timeout{event_loop, BIND_THIS_METHOD(OnTimeout)};

	Instance() noexcept {
		timeout.Schedule(std::chrono::seconds{10});
	}

	void OnTimeout() noexcept {
		event_loop.Break();
	}
};

} // anonymous namespace

/**
 * A response which is terminated by a trailer HEADERS frame must be
 * delivered to the #HttpResponseHandler exactly once.
 */
TEST(NgHttp2Client, Trailer)
{
	Instance instance;

	auto [client_fd, server_fd] = CreateStreamSocketPairNonBlock();

	FakeServer server{instance.event_loop, std::move(server_fd),
			  FakeServer::Mode::TRAILER};

	MyConnectionHandler connection_handler;
	NgHttp2::ClientConnection connection{
		std::make_unique<FilteredSocket>(instance.event_loop,
						 std::move(client_fd),
						 FdType::FD_SOCKET),
		connection_handler,
	};

	CountingHttpResponseHandler handler{instance.root_pool,
					    instance.event_loop};
	const AllocatorPtr alloc{handler.pool};

	StringMap request_headers;
	request_headers.Add(alloc, "host", "localhost");

	CancellablePointer cancel_ptr;
	connection.SendRequest(alloc, nullptr,
			       HttpMethod::GET, "/",
			       std::move(request_headers), {},
			       handler, cancel_ptr);

	instance.event_loop.Run();

	EXPECT_EQ(handler.n_responses, 1u);
	EXPECT_EQ(handler.state, RecordingHttpResponseHandler::State::END);
	EXPECT_EQ(handler.status, HttpStatus::OK);
	EXPECT_EQ(handler.body, response_body);
	EXPECT_FALSE(handler.error);
}

/**
 * The final response after a "100 Continue" must be delivered, not the
 * informational one.  nghttp2 reports that final response with category
 * NGHTTP2_HCAT_HEADERS, i.e. the same category as a trailer.
 */
TEST(NgHttp2Client, InformationalResponse)
{
	Instance instance;

	auto [client_fd, server_fd] = CreateStreamSocketPairNonBlock();

	FakeServer server{instance.event_loop, std::move(server_fd),
			  FakeServer::Mode::INFORMATIONAL};

	MyConnectionHandler connection_handler;
	NgHttp2::ClientConnection connection{
		std::make_unique<FilteredSocket>(instance.event_loop,
						 std::move(client_fd),
						 FdType::FD_SOCKET),
		connection_handler,
	};

	CountingHttpResponseHandler handler{instance.root_pool,
					    instance.event_loop};
	const AllocatorPtr alloc{handler.pool};

	StringMap request_headers;
	request_headers.Add(alloc, "host", "localhost");

	CancellablePointer cancel_ptr;
	connection.SendRequest(alloc, nullptr,
			       HttpMethod::GET, "/",
			       std::move(request_headers), {},
			       handler, cancel_ptr);

	instance.event_loop.Run();

	EXPECT_EQ(handler.n_responses, 1u);
	EXPECT_EQ(handler.status, HttpStatus::OK);
	EXPECT_EQ(handler.headers.count("x-final"), 1u);
	EXPECT_FALSE(handler.error);
}
