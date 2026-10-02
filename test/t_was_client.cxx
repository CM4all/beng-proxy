// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "t_client.hxx"
#include "stopwatch.hxx"
#include "was/Client.hxx"
#include "was/Server.hxx"
#include "was/Lease.hxx"
#include "was/async/Socket.hxx"
#include "lease.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/SuspendIstream.hxx"
#include "event/FineTimerEvent.hxx"
#include "strmap.hxx"
#include "util/SpanCast.hxx"

#include <was/protocol.h>

#include <string.h> // for memcpy()

using std::string_view_literals::operator""sv;

#include <functional>
#include <optional>

static void
RunNull(WasServer &server, struct pool &,
	HttpMethod,
	const char *, StringMap &&,
	UnusedIstreamPtr body)
{
	body.Clear();

	server.SendResponse(HttpStatus::NO_CONTENT, {}, nullptr);
}

static void
RunHello(WasServer &server, struct pool &pool,
	 HttpMethod,
	 const char *, StringMap &&,
	 UnusedIstreamPtr body)
{
	body.Clear();

	server.SendResponse(HttpStatus::OK, {},
			    istream_string_new(pool, "hello"));
}

static void
RunHuge(WasServer &server, struct pool &pool,
	HttpMethod ,
	const char *, StringMap &&,
	UnusedIstreamPtr body)
{
	body.Clear();

	server.SendResponse(HttpStatus::OK, {},
			    istream_head_new(pool,
					     istream_zero_new(pool),
					     524288, true));
}

static void
RunHold(WasServer &server, struct pool &pool,
	HttpMethod,
	const char *, StringMap &&,
	UnusedIstreamPtr body)
{
	body.Clear();

	server.SendResponse(HttpStatus::OK, {},
			    istream_block_new(pool));
}

static void
RunBlock(WasServer &server, struct pool &pool,
	 HttpMethod,
	 const char *, StringMap &&,
	 UnusedIstreamPtr body)
{
	body.Clear();

	server.SendResponse(HttpStatus::OK, {},
			    istream_block_new(pool));
}

static void
RunNop(WasServer &, struct pool &,
       HttpMethod ,
       const char *, StringMap &&,
       UnusedIstreamPtr) noexcept
{
}

static void
RunMirror(WasServer &server, struct pool &,
	  HttpMethod,
	  const char *, StringMap &&headers,
	  UnusedIstreamPtr body)
{
	const bool has_body = body;
	server.SendResponse(has_body ? HttpStatus::OK : HttpStatus::NO_CONTENT,
			    std::move(headers), std::move(body));
}

static void
RunMalformedHeaderName(WasServer &server, struct pool &pool,
		       HttpMethod, const char *, StringMap &&,
		       UnusedIstreamPtr body)
{
	body.Clear();

	StringMap response_headers(pool, {{"header name", "foo"}});

	server.SendResponse(HttpStatus::NO_CONTENT,
			    std::move(response_headers), nullptr);
}

static void
RunMalformedHeaderValue(WasServer &server, struct pool &pool,
			HttpMethod, const char *, StringMap &&,
			UnusedIstreamPtr body)
{
	body.Clear();

	StringMap response_headers(pool, {{"name", "foo\nbar"}});

	server.SendResponse(HttpStatus::NO_CONTENT,
			    std::move(response_headers), nullptr);
}

static void
RunValidPremature(WasServer &server, struct pool &pool,
		  HttpMethod,
		  const char *, StringMap &&,
		  UnusedIstreamPtr body)
{
	body.Clear();

	server.SendResponse(HttpStatus::OK, {},
			    NewConcatIstream(pool,
					     istream_head_new(pool,
							      istream_zero_new(pool),
							      512, true),
					     NewSuspendIstream(pool, istream_fail_new(pool, std::make_exception_ptr(std::runtime_error("Error"))),
							       server.GetEventLoop(),
							       std::chrono::milliseconds(10))));
}

class MalformedPrematureWasServer final : Was::ControlHandler {
public:
	enum class Mode {
		/**
		 * Announce a 1 kB response body, then send a PREMATURE
		 * packet with a larger value.
		 */
		MALFORMED_PREMATURE,

		/**
		 * Send DATA immediately followed by NO_DATA.  Both
		 * packets are written at once, so the client
		 * evaluates the second one while the response is
		 * still "pending", i.e. after DATA but before the
		 * response was passed to the #HttpResponseHandler.
		 */
		PENDING_ERROR,

		/**
		 * Announce a response body, and send the last of it
		 * together with a PREMATURE packet; the control
		 * channel is written first, so the client defers the
		 * PREMATURE and then releases the pipe (and the WAS
		 * process) while that deferred update is still
		 * pending.
		 */
		PREMATURE_AT_END,
	};

private:
	WasSocket socket;

	Was::Control control;

	FineTimerEvent defer_premature;

	const Mode mode;

	WasServerHandler &handler;

public:
	MalformedPrematureWasServer(EventLoop &event_loop,
				    WasSocket &&_socket,
				    WasServerHandler &_handler,
				    Mode _mode=Mode::MALFORMED_PREMATURE) noexcept
		:socket(std::move(_socket)),
		 control(event_loop, std::move(socket.control), *this),
		 defer_premature(event_loop, BIND_THIS_METHOD(SendPremature)),
		 mode(_mode),
		 handler(_handler)
		{
		}

	void Free() noexcept {
		ReleaseError();
	}

	void SendResponse(HttpStatus status,
			  StringMap &&headers, UnusedIstreamPtr body) noexcept;

private:
	void Destroy() noexcept {
		this->~MalformedPrematureWasServer();
	}

	void ReleaseError() noexcept {
		socket.Close();
		Destroy();
	}

	void ReleaseUnused() noexcept;

	void AbortError() noexcept {
		auto &handler2 = handler;
		ReleaseError();
		handler2.OnWasClosed();
	}

	/**
	 * Abort receiving the response status/headers from the WAS server.
	 */
	void AbortUnused() noexcept {
		auto &handler2 = handler;
		ReleaseUnused();
		handler2.OnWasClosed();
	}

protected:
	void SendPremature() noexcept {
		if (mode == Mode::PREMATURE_AT_END) {
			/* write the control packet directly to the
			   socket, because Was::Control would only defer
			   the write; this way, the control channel
			   becomes readable before the pipe ... */
			const struct was_header header{
				.length = sizeof(uint64_t),
				.command = WAS_COMMAND_PREMATURE,
			};

			static constexpr uint64_t premature_length = 3;

			std::byte buffer[sizeof(header) + sizeof(premature_length)];
			memcpy(buffer, &header, sizeof(header));
			memcpy(buffer + sizeof(header), &premature_length,
			       sizeof(premature_length));
			control.GetSocket().Send(buffer);

			/* ... and only then complete the announced
			   response body, so the client releases the pipe
			   while the PREMATURE packet is still deferred */
			socket.output.Write(AsBytes("hello"sv));
			return;
		}

		/* the response body was announced as 1 kB - and now
		   we tell the client he already sent 4 kB */
		control.SendUint64(WAS_COMMAND_PREMATURE, 4096);
	}

	/* virtual methods from class Was::ControlHandler */
	bool OnWasControlPacket(enum was_command cmd,
				std::span<const std::byte> payload) noexcept override;
	bool OnWasControlDrained() noexcept override {
		return true;
	}
	void OnWasControlDone() noexcept override {}
	void OnWasControlHangup() noexcept override {
		AbortError();
	}
	void OnWasControlError(std::exception_ptr) noexcept override {
		AbortError();
	}
};

bool
MalformedPrematureWasServer::OnWasControlPacket(enum was_command cmd,
						std::span<const std::byte> payload) noexcept
{
	(void)payload;

	switch (cmd) {
	case WAS_COMMAND_NOP:
	case WAS_COMMAND_REQUEST:
	case WAS_COMMAND_METHOD:
	case WAS_COMMAND_URI:
	case WAS_COMMAND_SCRIPT_NAME:
	case WAS_COMMAND_PATH_INFO:
	case WAS_COMMAND_QUERY_STRING:
	case WAS_COMMAND_HEADER:
	case WAS_COMMAND_PARAMETER:
	case WAS_COMMAND_REMOTE_HOST:
	case WAS_COMMAND_DOCUMENT_ROOT:
	case WAS_COMMAND_TLS:
		break;

	case WAS_COMMAND_STATUS:
		AbortError();
		return false;

	case WAS_COMMAND_NO_DATA:
	case WAS_COMMAND_DATA:
		switch (mode) {
		case Mode::MALFORMED_PREMATURE:
			/* announce a response body of 1 kB */
			if (!control.Send(WAS_COMMAND_DATA) ||
			    !control.SendUint64(WAS_COMMAND_LENGTH, 1024))
				return false;

			defer_premature.Schedule(std::chrono::milliseconds(1));
			break;

		case Mode::PENDING_ERROR:
			/* announce a response body and contradict it
			   right away; both packets are flushed
			   together, so the client sees the NO_DATA
			   while the response is still "pending" */
			if (!control.Send(WAS_COMMAND_DATA) ||
			    !control.Send(WAS_COMMAND_NO_DATA))
				return false;

			break;

		case Mode::PREMATURE_AT_END:
			/* announce a 5 byte response body, but send
			   it later (see SendPremature()) */
			if (!control.Send(WAS_COMMAND_DATA) ||
			    !control.SendUint64(WAS_COMMAND_LENGTH, 5))
				return false;

			defer_premature.Schedule(std::chrono::milliseconds(10));
			break;
		}

		return true;

	case WAS_COMMAND_LENGTH:
		break;

	case WAS_COMMAND_STOP:
	case WAS_COMMAND_PREMATURE:
	case WAS_COMMAND_METRIC:
		break;
	}

	return true;
}

class WasConnection final
	: public ClientConnection, WasServerHandler, WasLease, Was::ControlHandler
{
	EventLoop &event_loop;

	WasSocket socket;
	std::optional<Was::Control> control;

	WasServer *server = nullptr;

	MalformedPrematureWasServer *server2 = nullptr;

	Lease *lease;

	typedef std::function<void(WasServer &server, struct pool &pool,
				   HttpMethod method,
				   const char *uri, StringMap &&headers,
				   UnusedIstreamPtr body)> Callback;

	const Callback callback;

public:
	WasConnection(struct pool &pool, EventLoop &_event_loop,
		      Callback &&_callback)
		:event_loop(_event_loop),
		 callback(std::move(_callback))
	{
		WasServerHandler &handler = *this;
		server = NewFromPool<WasServer>(pool, pool, event_loop,
						MakeWasSocket(),
						handler);
	}

	struct MalformedPremature{};

	WasConnection(struct pool &pool, EventLoop &_event_loop,
		      MalformedPremature)
		:event_loop(_event_loop)
	{
		WasServerHandler &handler = *this;
		server2 = NewFromPool<MalformedPrematureWasServer>(pool, event_loop,
								   MakeWasSocket(),
								   handler);
	}

	struct PendingError{};

	struct PrematureAtEnd{};

	WasConnection(struct pool &pool, EventLoop &_event_loop,
		      PrematureAtEnd)
		:event_loop(_event_loop)
	{
		WasServerHandler &handler = *this;
		server2 = NewFromPool<MalformedPrematureWasServer>(pool, event_loop,
								   MakeWasSocket(),
								   handler,
								   MalformedPrematureWasServer::Mode::PREMATURE_AT_END);
	}

	WasConnection(struct pool &pool, EventLoop &_event_loop,
		      PendingError)
		:event_loop(_event_loop)
	{
		WasServerHandler &handler = *this;
		server2 = NewFromPool<MalformedPrematureWasServer>(pool, event_loop,
								   MakeWasSocket(),
								   handler,
								   MalformedPrematureWasServer::Mode::PENDING_ERROR);
	}

	~WasConnection() noexcept override {
		if (server != nullptr)
			server->Free();
		if (server2 != nullptr)
			server2->Free();
	}

	auto &GetEventLoop() const noexcept {
		return event_loop;
	}

	void Request(struct pool &pool,
		     Lease &_lease,
		     HttpMethod method, const char *uri,
		     StringMap &&headers, UnusedIstreamPtr body,
		     [[maybe_unused]] bool expect_100,
		     HttpResponseHandler &handler,
		     CancellablePointer &cancel_ptr) noexcept override {
		lease = &_lease;
		was_client_request(pool, nullptr,
				   *control, socket.input, socket.output,
				   *this,
				   nullptr, false, nullptr,
				   method, uri, uri, nullptr, nullptr,
				   headers, std::move(body), {},
				   nullptr,
				   handler, cancel_ptr);
	}

	void InjectSocketFailure() noexcept override {
		control->GetSocket().Shutdown();
	}

	/* virtual methods from class WasServerHandler */

	void OnWasRequest(struct pool &pool, HttpMethod method,
			  const char *uri, StringMap &&headers,
			  UnusedIstreamPtr body) noexcept override {
		callback(*server, pool, method, uri,
			 std::move(headers), std::move(body));
	}

	void OnWasClosed() noexcept override {
		server = nullptr;
		server2 = nullptr;
	}

private:
	WasSocket MakeWasSocket() {
		auto s = WasSocket::CreatePair();

		socket = std::move(s.first);
		socket.input.SetNonBlocking();
		socket.output.SetNonBlocking();
		control.emplace(event_loop, std::move(socket.control), static_cast<Was::ControlHandler &>(*this));

		s.second.input.SetNonBlocking();
		s.second.output.SetNonBlocking();
		return std::move(s.second);
	}

	void OnCloseTimer() noexcept {
		if (server != nullptr)
			std::exchange(server, nullptr)->Free();
		if (server2 != nullptr)
			std::exchange(server2, nullptr)->Free();
	}

	/* virtual methods from class WasLease */
	PutAction ReleaseWas(PutAction action) noexcept override {
		return lease->ReleaseLease(action);
	}

	PutAction ReleaseWasStop(uint_least64_t) noexcept override {
		return ReleaseWas(PutAction::DESTROY);
	}

	/* virtual methods from class WasControlHandler */
	bool OnWasControlPacket(enum was_command,
				std::span<const std::byte>) noexcept override {
		return true;
	}
	bool OnWasControlDrained() noexcept override {
		return true;
	}
	void OnWasControlDone() noexcept override {}
	void OnWasControlHangup() noexcept override {}
	void OnWasControlError(std::exception_ptr) noexcept override {}
};

struct WasFactory {
	static constexpr ClientTestOptions options{
		.have_chunked_request_body = true,
		.can_cancel_request_body = true,
		.enable_valid_premature = true,
		.enable_malformed_premature = true,
		.no_early_release_socket = true, // TODO: improve the WAS client
	};

	explicit WasFactory(EventLoop &) noexcept {}

	auto *NewMirror(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunMirror);
	}

	auto *NewNull(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunNull);
	}

	auto *NewDummy(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunHello);
	}

	auto *NewFixed(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunHello);
	}

	auto *NewTiny(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunHello);
	}

	auto *NewHuge(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunHuge);
	}

	auto *NewHold(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunHold);
	}

	auto *NewBlock(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunBlock);
	}

	auto *NewNop(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunNop);
	}

	auto *NewMalformedHeaderName(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunMalformedHeaderName);
	}

	auto *NewMalformedHeaderValue(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunMalformedHeaderValue);
	}

	auto *NewValidPremature(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop, RunValidPremature);
	}

	auto *NewMalformedPremature(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop,
					 WasConnection::MalformedPremature{});
	}

	auto *NewPendingError(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop,
					 WasConnection::PendingError{});
	}

	auto *NewPrematureAtEnd(struct pool &pool, EventLoop &event_loop) {
		return new WasConnection(pool, event_loop,
					 WasConnection::PrematureAtEnd{});
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(WasClient, ClientTest, WasFactory);

TEST(WasClient, MalformedHeaderName)
{
	Instance instance;
	WasFactory factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewMalformedHeaderName(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,

			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_EQ(c.status, HttpStatus{});
	EXPECT_TRUE(c.request_error);
	EXPECT_TRUE(c.released);
}

TEST(WasClient, MalformedHeaderValue)
{
	Instance instance;
	WasFactory factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewMalformedHeaderValue(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,

			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_EQ(c.status, HttpStatus{});
	EXPECT_TRUE(c.request_error);
	EXPECT_TRUE(c.released);
}

/**
 * A protocol error which arrives after DATA, but before the response
 * was submitted to the #HttpResponseHandler ("pending" state), must
 * be reported to the handler instead of tripping an assertion.
 */
TEST(WasClient, PendingError)
{
	Instance instance;
	WasFactory factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewPendingError(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,

			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_EQ(c.status, HttpStatus{});
	EXPECT_TRUE(c.request_error);
	EXPECT_TRUE(c.released);
}

/**
 * A PREMATURE packet which was received before the last chunk of the
 * response body must not be applied to the #WasInput after its pipe
 * (and the WAS process lease) have been released.
 */
TEST(WasClient, PrematureAfterPipeRelease)
{
	Instance instance;
	WasFactory factory{instance.event_loop};
	Context c{instance};

	/* don't consume the response body, so the #WasInput is still
	   alive when the deferred PREMATURE packet is evaluated */
	c.data_blocking = 1;

	c.connection = factory.NewPrematureAtEnd(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	/* the client has received the whole announced response body
	   and has released the WAS process */
	EXPECT_FALSE(c.request_error);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.body_data, 5u);
	EXPECT_FALSE(c.body_eof);
	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.lease_action, PutAction::REUSE);

	/* now the deferred PREMATURE packet is evaluated; it must not
	   touch the released pipe and must not release the lease a
	   second time */
	c.event_loop.Run();

	/* the packet was discarded, so no error was reported */
	EXPECT_FALSE(c.body_error);
	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.lease_action, PutAction::REUSE);
}
