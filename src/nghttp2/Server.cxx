/*
 * Copyright 2007-2019 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Server.hxx"
#include "Util.hxx"
#include "IstreamDataSource.hxx"
#include "Option.hxx"
#include "Callbacks.hxx"
#include "pool/pool.hxx"
#include "pool/PSocketAddress.hxx"
#include "http_server/Handler.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Headers.hxx"
#include "istream/LengthIstream.hxx"
#include "istream/MultiFifoBufferIstream.hxx"
#include "istream/New.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "util/RuntimeError.hxx"
#include "util/StaticArray.hxx"
#include "util/StringView.hxx"
#include "address_string.hxx"
#include "stopwatch.hxx"

#include <nghttp2/nghttp2.h>

#include <assert.h>

namespace NgHttp2 {

static constexpr Event::Duration write_timeout = std::chrono::seconds(30);

class ServerConnection::Request final
	: public IncomingHttpRequest, MultiFifoBufferIstreamHandler,
	  public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>
{
	ServerConnection &connection;

	const uint32_t id;

	bool eof = false;

	CancellablePointer cancel_ptr;

	MultiFifoBufferIstream *request_body_control = nullptr;

	std::unique_ptr<IstreamDataSource> response_body;

	RootStopwatchPtr stopwatch;

public:
	explicit Request(PoolPtr &&_pool,
			 ServerConnection &_connection, uint32_t _id) noexcept
		:IncomingHttpRequest(std::move(_pool),
				     _connection.local_address,
				     _connection.remote_address,
				     _connection.local_host_and_port,
				     _connection.remote_host),
		 connection(_connection), id(_id)
	{
		method = HTTP_METHOD_GET;
	}

	~Request() noexcept {
		/* clear stream_user_data to ignore future callbacks
		   on this stream */
		nghttp2_session_set_stream_user_data(connection.session.get(),
						     id, nullptr);

		if (request_body_control)
			request_body_control->DestroyError(std::make_exception_ptr(std::runtime_error("Canceled")));

		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	void Destroy() noexcept {
		pool_trash(pool);
		this->~Request();
	}

	nghttp2_data_provider MakeResponseDataProvider(UnusedIstreamPtr &&istream) noexcept {
		assert(!response_body);
		assert(istream);

		response_body = std::make_unique<IstreamDataSource>(connection.session.get(), id,
								    std::move(istream));
		return response_body->MakeDataProvider();
	}

	int OnReceiveRequest(bool has_request_body) noexcept;

	int OnEndDataFrame() noexcept;

	int OnStreamCloseCallback(uint32_t error_code) noexcept {
		if (request_body_control) {
			auto error = FormatRuntimeError("Stream closed: %s",
							nghttp2_http2_strerror(error_code));
			request_body_control->DestroyError(std::make_exception_ptr(std::move(error)));
			request_body_control = nullptr;
		}

		Destroy();
		return 0;
	}

	static int OnStreamCloseCallback(nghttp2_session *session,
					 int32_t stream_id,
					 uint32_t error_code, void *) noexcept {
		auto *request = (Request *)
			nghttp2_session_get_stream_user_data(session, stream_id);
		if (request == nullptr)
			return 0;

		return request->OnStreamCloseCallback(error_code);
	}

	int OnHeaderCallback(StringView name, StringView value) noexcept;

	static int OnHeaderCallback(nghttp2_session *session,
				    const nghttp2_frame *frame,
				    const uint8_t *name, size_t namelen,
				    const uint8_t *value, size_t valuelen,
				    uint8_t, void *) noexcept {
		if (frame->hd.type != NGHTTP2_HEADERS ||
		    frame->headers.cat != NGHTTP2_HCAT_REQUEST)
			return 0;

		auto *request = (Request *)
			nghttp2_session_get_stream_user_data(session, frame->hd.stream_id);
		if (request == nullptr)
			return 0;

		return request->OnHeaderCallback({(const char *)name, namelen},
						 {(const char *)value, valuelen});
	}

	int OnDataChunkReceivedCallback(ConstBuffer<uint8_t> data,
					uint8_t flags) noexcept;

	static int OnDataChunkReceivedCallback(nghttp2_session *session,
					       uint8_t flags,
					       int32_t stream_id,
					       const uint8_t *data,
					       size_t len,
					       void */*user_data*/) noexcept {
		auto *request = (Request *)
			nghttp2_session_get_stream_user_data(session, stream_id);
		if (request == nullptr)
			return 0;

		return request->OnDataChunkReceivedCallback({data, len}, flags);
	}

private:
	/* virtual methods from class MultiFifoBufferIstreamHandler */
	void OnFifoBufferIstreamConsumed(size_t nbytes) noexcept override {
		nghttp2_session_consume(connection.session.get(), id, nbytes);
	}

	void OnFifoBufferIstreamClosed() noexcept override {
		assert(request_body_control);

		request_body_control = nullptr;
	}

	/* virtual methods from class IncomingHttpRequest */
	void SendResponse(http_status_t status,
			  HttpHeaders &&response_headers,
			  UnusedIstreamPtr response_body) noexcept override;
};

static http_method_t
ParseHttpMethod(StringView s) noexcept
{
	for (size_t i = 0; i < size_t(HTTP_METHOD_INVALID); ++i) {
		const char *name = http_method_to_string_data[i];
		if (name != nullptr && s.Equals(name))
			return http_method_t(i);
	}

	return HTTP_METHOD_GET;
}

inline int
ServerConnection::Request::OnHeaderCallback(StringView name,
					    StringView value) noexcept
{
	AllocatorPtr alloc(pool);

	if (name.Equals(":method"))
		method = ParseHttpMethod(value);
	else if (name.Equals(":path"))
		uri = p_strdup(pool, value);
	else if (name.Equals(":authority"))
		headers.Add(alloc, "host", p_strdup(pool, value));
	else if (name.size >= 2 && name.front() != ':')
		headers.Add(alloc, p_strdup_lower(pool, name), p_strdup(pool, value));

	return 0;
}

inline int
ServerConnection::Request::OnDataChunkReceivedCallback(ConstBuffer<uint8_t> data,
						       uint8_t flags) noexcept
{
	// TODO: limit the MultiFifoBuffer size

	if (!request_body_control)
		return 0;

	request_body_control->Push(data.ToVoid());

	eof = (flags & NGHTTP2_FLAG_END_STREAM) != 0;
	if (eof) {
		request_body_control->SetEof();
		return 0;
	}

	request_body_control->SubmitBuffer();

	return 0;
}

int
ServerConnection::Request::OnReceiveRequest(bool has_request_body) noexcept
{
	if (method == HTTP_METHOD_NULL || uri == nullptr) {
		/* no method and no URI - refuse to handle this
		   request */
		nghttp2_submit_rst_stream(connection.session.get(),
					  NGHTTP2_FLAG_NONE,
					  id, NGHTTP2_CANCEL);
		Destroy();
		return 0;
	}

	if (has_request_body) {
		MultiFifoBufferIstreamHandler &fbi_handler = *this;
		request_body_control = NewFromPool<MultiFifoBufferIstream>(pool, pool, fbi_handler);
		body = UnusedIstreamPtr(request_body_control);

		const char *content_length = headers.Remove("content-length");
		if (content_length != nullptr) {
			char *endptr;
			auto length = strtoul(content_length, &endptr, 10);
			if (endptr > content_length)
				body = NewIstreamPtr<LengthIstream>(pool,
								    std::move(body),
								    length);
		}
	}

	stopwatch = RootStopwatchPtr(uri);

	connection.handler.HandleHttpRequest(*this,
					     stopwatch,
					     cancel_ptr);

	return 0;
}

int
ServerConnection::Request::OnEndDataFrame() noexcept
{
	if (!request_body_control || eof)
		return 0;

	eof = true;

	request_body_control->SetEof();
	return 0;
}

void
ServerConnection::Request::SendResponse(http_status_t status,
					HttpHeaders &&response_headers,
					UnusedIstreamPtr _response_body) noexcept
{
	cancel_ptr = nullptr;

	char status_string[16];
	sprintf(status_string, "%u", unsigned(status));

	StaticArray<nghttp2_nv, 256> hdrs;
	hdrs.push_back(MakeNv(":status", status_string));

	char content_length_string[32];
	if (_response_body) {
		const auto content_length = _response_body.GetAvailable(false);
		if (content_length >= 0) {
			snprintf(content_length_string,
				 sizeof(content_length_string),
				 "%lu", (unsigned long)content_length);
			hdrs.push_back(MakeNv("content-length",
					      content_length_string));
		}
	}

	for (const auto &i : std::move(response_headers).ToMap(pool)) {
		if (hdrs.full())
			// TODO: what now?
			break;

		hdrs.push_back(MakeNv(i.key, i.value));
	}

	nghttp2_data_provider dp, *dpp = nullptr;
	if (_response_body) {
		dp = MakeResponseDataProvider(std::move(_response_body));
		dpp = &dp;
	}

	nghttp2_submit_response(connection.session.get(), id,
				hdrs.raw(), hdrs.size(),
				dpp);
}

ServerConnection::ServerConnection(struct pool &_pool, EventLoop &loop,
				   UniqueSocketDescriptor fd,
				   FdType fd_type,
				   SocketFilterPtr filter,
				   SocketAddress _remote_address,
				   HttpServerConnectionHandler &_handler)
	:pool(_pool), socket(loop),
	 handler(_handler),
	 local_address(DupAddress(pool, fd.GetLocalAddress())),
	 remote_address(DupAddress(pool, _remote_address)),
	 local_host_and_port(address_to_string(pool, local_address)),
	 remote_host(address_to_host_string(pool, remote_address))
{
	socket.Init(fd.Release(), fd_type,
		    Event::Duration(-1), write_timeout,
		    std::move(filter),
		    *this);

	NgHttp2::Option option;
	//nghttp2_option_set_recv_client_preface(option.get(), 1);
	nghttp2_option_set_no_auto_window_update(option.get(), true);

	NgHttp2::SessionCallbacks callbacks;
	nghttp2_session_callbacks_set_send_callback(callbacks.get(), SendCallback);
	nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks.get(),
							     OnFrameRecvCallback);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks.get(),
							       Request::OnStreamCloseCallback);
	nghttp2_session_callbacks_set_on_header_callback(callbacks.get(),
							 Request::OnHeaderCallback);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks.get(),
								  Request::OnDataChunkReceivedCallback);
	nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks.get(),
								OnBeginHeaderCallback);

	session = NgHttp2::Session::NewServer(callbacks.get(), this, option.get());

	static constexpr nghttp2_settings_entry iv[1] = {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 256}};

	const auto rv = nghttp2_submit_settings(session.get(), NGHTTP2_FLAG_NONE,
						iv, std::size(iv));
	if (rv != 0)
		throw FormatRuntimeError("nghttp2_submit_settings() failed: %s",
					 nghttp2_strerror(rv));

	// TODO: idle_timeout.Schedule(http_server_idle_timeout);

	socket.ScheduleReadNoTimeout(false);
}

ServerConnection::~ServerConnection() noexcept
{
	requests.clear_and_dispose([](Request *request) { request->Destroy(); });
}

ssize_t
ServerConnection::SendCallback(const void *data, size_t length) noexcept
{
	const auto nbytes = socket.Write(data, length);
	if (nbytes < 0) {
		const int e = errno;
		switch (e) {
		case EAGAIN:
			socket.ScheduleWrite();
			return NGHTTP2_ERR_WOULDBLOCK;
		}
	}

	return nbytes;
}

int
ServerConnection::OnFrameRecvCallback(const nghttp2_frame *frame) noexcept
{
	switch (frame->hd.type) {
	case NGHTTP2_HEADERS:
		if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
			void *stream_data = nghttp2_session_get_stream_user_data(session.get(),
										 frame->hd.stream_id);
			if (stream_data == nullptr)
				return 0;

			auto &request = *(Request *)stream_data;
			return request.OnReceiveRequest((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) == 0);
		}
		break;

	case NGHTTP2_DATA:
		if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
			void *stream_data = nghttp2_session_get_stream_user_data(session.get(),
										 frame->hd.stream_id);
			if (stream_data == nullptr)
				return 0;

			auto &request = *(Request *)stream_data;
			return request.OnEndDataFrame();
		}

		break;

	default:
		break;
	}
	return 0;
}

int
ServerConnection::OnBeginHeaderCallback(const nghttp2_frame *frame) noexcept
{
	if (frame->hd.type == NGHTTP2_HEADERS ||
	    frame->headers.cat == NGHTTP2_HCAT_REQUEST) {

		auto stream_pool = pool_new_linear(&pool,
						   "NgHttp2ServerRequest", 8192);
		pool_set_major(stream_pool);

		auto *request = NewFromPool<Request>(std::move(stream_pool),
						     *this, frame->hd.stream_id);
		requests.push_front(*request);
		nghttp2_session_set_stream_user_data(session.get(),
						     frame->hd.stream_id,
						     request);
		return 0;
	} else
		return 0;
}

BufferedResult
ServerConnection::OnBufferedData()
{
	auto r = socket.ReadBuffer();

	auto nbytes = nghttp2_session_mem_recv(session.get(),
					       (const uint8_t *)r.data, r.size);
	if (nbytes < 0)
		throw FormatRuntimeError("nghttp2_session_mem_recv() failed: %s",
					 nghttp2_strerror((int)nbytes));

	socket.DisposeConsumed(nbytes);

	const auto rv = nghttp2_session_send(session.get());
	if (rv != 0)
		throw FormatRuntimeError("nghttp2_session_send() failed: %s",
					 nghttp2_strerror(rv));

	return BufferedResult::MORE; // TODO?
}

bool
ServerConnection::OnBufferedClosed() noexcept
{
	// TODO
	handler.HttpConnectionClosed();
	return false;
}

bool
ServerConnection::OnBufferedWrite()
{
	const auto rv = nghttp2_session_send(session.get());
	if (rv != 0)
		throw FormatRuntimeError("nghttp2_session_send() failed: %s",
					 nghttp2_strerror(rv));

	if (!nghttp2_session_want_write(session.get()))
		socket.UnscheduleWrite();

	return true;
}

void
ServerConnection::OnBufferedError(std::exception_ptr e) noexcept
{
	handler.HttpConnectionError(std::move(e));
}

} // namespace NgHttp2
