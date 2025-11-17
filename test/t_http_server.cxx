// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "FlushEventLoop.hxx"
#include "NullSocketFilterFactory.hxx"
#include "NopThreadSocketFilterFactory.hxx"
#include "NopSocketFilterFactory.hxx"
#include "http/server/Public.hxx"
#include "http/server/Handler.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Client.hxx"
#include "http/Headers.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"
#include "lease.hxx"
#include "pool/pool.hxx"
#include "pool/Holder.hxx"
#include "pool/UniquePtr.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/InjectIstream.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ZeroIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/New.hxx"
#include "istream/NoBucketIstream.hxx"
#include "istream/NoLengthIstream.hxx"
#include "istream/NullSink.hxx"
#include "istream/StringSink.hxx"
#include "memory/GrowingBuffer.hxx"
#include "memory/SinkGrowingBuffer.hxx"
#include "memory/SlicePool.hxx"
#include "memory/istream_gb.hxx"
#include "fs/FilteredSocket.hxx"
#include "event/FineTimerEvent.hxx"
#include "net/SocketPair.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

#include <functional>
#include <optional>
#include <utility> // for std::unreachable()

#include <stdio.h>
#include <stdlib.h>

using std::string_view_literals::operator""sv;

class Server final
	: PoolHolder,
	  HttpServerConnectionHandler, HttpServerRequestHandler,
	  Lease, BufferedSocketHandler
{
	SlicePool request_slice_pool{8192, 256, "Requests"};

	HttpServerConnection *connection = nullptr;

	std::function<void(IncomingHttpRequest &request,
			   CancellablePointer &cancel_ptr)> request_handler;

	FilteredSocket client_fs;

	bool has_filter;

	bool client_fs_released = false;

	bool break_closed = false;

public:
	Server(struct pool &_pool, EventLoop &event_loop,
	       SocketFilterFactory &factory);

	~Server() noexcept {
		CloseClientSocket();
		CheckCloseConnection();
	}

	bool HasFilter() const noexcept {
		return has_filter;
	}

	using PoolHolder::GetPool;

	auto &GetEventLoop() noexcept {
		return client_fs.GetEventLoop();
	}

	void SetRequestHandler(std::invocable<IncomingHttpRequest &, CancellablePointer &> auto handler) noexcept {
		request_handler = std::move(handler);
	}

	void CloseConnection() noexcept {
		http_server_connection_close(connection);
		connection = nullptr;
	}

	void CheckCloseConnection() noexcept {
		if (connection != nullptr)
			CloseConnection();
	}

	void SendRequest(HttpMethod method, const char *uri,
			 const StringMap &headers,
			 UnusedIstreamPtr body, bool expect_100,
			 HttpResponseHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept {
		http_client_request(*pool, nullptr, client_fs, *this,
				    "foo",
				    method, uri, headers, {},
				    std::move(body), expect_100,
				    handler, cancel_ptr);
	}

	void CloseClientSocket() noexcept {
		if (client_fs.IsValid()) {
			if (client_fs.IsConnected())
				client_fs.Close();
			client_fs.Destroy();
		}
	}

	void WaitClosed() {
		if (connection == nullptr)
			return;

		break_closed = true;
		GetEventLoop().Run();
		break_closed = false;

		ASSERT_EQ(connection, nullptr);
	}

private:
	/* virtual methods from class HttpServerConnectionHandler */
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;
	void HttpConnectionError(std::exception_ptr e) noexcept override;
	void HttpConnectionClosed() noexcept override;

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override {
		client_fs_released = true;

		if (action == PutAction::REUSE && client_fs.IsValid() &&
		    client_fs.IsConnected()) {
			client_fs.Reinit(Event::Duration(-1), *this);
			client_fs.UnscheduleWrite();
			return PutAction::REUSE;
		} else {
			CloseClientSocket();
			return PutAction::DESTROY;
		}
	}

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override {
		fprintf(stderr, "unexpected data in idle TCP connection");
		CloseClientSocket();
		return BufferedResult::DESTROYED;
	}

	bool OnBufferedClosed() noexcept override {
		CloseClientSocket();
		return false;
	}

	[[gnu::noreturn]]
	bool OnBufferedWrite() override {
		/* should never be reached because we never schedule
		   writing */
		std::unreachable();
	}

	void OnBufferedError(std::exception_ptr e) noexcept override {
		PrintException(e);
		CloseClientSocket();
	}
};

class Client final : HttpResponseHandler, StringSinkHandler {
	EventLoop &event_loop;

	struct pool *pool;

	CancellablePointer cancel_ptr;

	std::exception_ptr response_error;
	std::string response_body;
	HttpStatus status{};

	bool break_done = false;

public:
	explicit Client(EventLoop &_event_loop) noexcept
		:event_loop(_event_loop) {}

	void SendRequest(Server &server,
			 HttpMethod method, const char *uri,
			 const StringMap &headers,
			 UnusedIstreamPtr body, bool expect_100=false) noexcept {
		pool = &server.GetPool();

		server.SendRequest(method, uri, headers,
				   std::move(body), expect_100,
				   *this, cancel_ptr);
	}

	void Cancel() noexcept {
		cancel_ptr.Cancel();
	}

	bool IsDone() const noexcept {
		return !cancel_ptr;
	}

	void WaitDone() {
		if (IsDone())
			return;

		break_done = true;
		event_loop.Run();
		break_done = false;

		ASSERT_TRUE(IsDone());
	}

	void RethrowResponseError() const {
		if (response_error)
			std::rethrow_exception(response_error);
	}

	void AssertResponse(HttpStatus expected_status,
			    std::string_view expected_body) {
		RethrowResponseError();

		EXPECT_EQ(status, expected_status);
		EXPECT_EQ(response_body, expected_body);
	}

	void ExpectResponse(HttpStatus expected_status,
			    std::string_view expected_body) {
		WaitDone();
		AssertResponse(expected_status, expected_body);
	}

private:
	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus _status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override {
		cancel_ptr = {};

		status = _status;

		(void)headers;

		if (body) {
			NewStringSink(*pool, std::move(body), *this, cancel_ptr);
		} else {
			if (break_done)
				event_loop.Break();
		}
	}

	void OnHttpError(std::exception_ptr ep) noexcept override {
		response_error = std::move(ep);
	}

	/* virtual methods from class StringSink */

	void OnStringSinkSuccess(std::string &&value) noexcept override {
		cancel_ptr = {};
		response_body = std::move(value);

		if (break_done)
			event_loop.Break();
	}

	void OnStringSinkError(std::exception_ptr error) noexcept override {
		cancel_ptr = {};
		response_error = std::move(error);

		if (break_done)
			event_loop.Break();
	}
};

Server::Server(struct pool &_pool, EventLoop &event_loop,
	       SocketFilterFactory &factory)
	:PoolHolder(pool_new_libc(&_pool, "catch")),
	 client_fs(event_loop)
{
	auto [client_socket, server_socket] = CreateStreamSocketPair();

	auto filter = factory.CreateFilter();
	has_filter = !!filter;

	auto socket = UniquePoolPtr<FilteredSocket>::Make(pool,
							  event_loop,
							  std::move(server_socket),
							  FdType::FD_SOCKET,
							  std::move(filter));
	if (has_filter)
		/* when there's a filter, reading must always be
		   scheduled, but that is usually done by
		   FilteredSocketListener which we skip here */
		socket->ScheduleRead();

	connection = http_server_connection_new(pool,
						std::move(socket),
						nullptr, nullptr,
						true,
						request_slice_pool,
						*this, *this);

	client_fs.InitDummy(std::move(client_socket), FdType::FD_SOCKET);
}

void
Server::HandleHttpRequest(IncomingHttpRequest &request,
			  const StopwatchPtr &,
			  CancellablePointer &cancel_ptr) noexcept
{
	request_handler(request, cancel_ptr);
}

void
Server::HttpConnectionError(std::exception_ptr e) noexcept
{
	connection = nullptr;

	PrintException(e);

	if (break_closed)
		GetEventLoop().Break();
}

void
Server::HttpConnectionClosed() noexcept
{
	connection = nullptr;

	if (break_closed)
		GetEventLoop().Break();
}

template<typename F>
class HttpServerTest : public testing::Test {
	static constexpr F MakeFactory(EventLoop &event_loop) {
		if constexpr (requires{ F{event_loop}; })
			return F{event_loop};
		else
			return F{};
	}

protected:
	TestInstance instance_;
	F factory_ = MakeFactory(instance_.event_loop);

	Server MakeServer() {
		return {
			instance_.root_pool,
			instance_.event_loop,
			factory_,
		};
	}

public:
	void FlushFilters() noexcept {
		if constexpr (requires{ factory_.Flush(); })
			factory_.Flush();
	}
};

using Factories = ::testing::Types<NullSocketFilterFactory, NopSocketFilterFactory, NopThreadSocketFilterFactory>;
TYPED_TEST_SUITE(HttpServerTest, Factories);

class CommonHandler : Cancellable {
	Server &server;

	IncomingHttpRequest *request_ptr;
	UnusedHoldIstreamPtr request_body;

	const bool buckets;

	bool canceled = false;

public:
	CommonHandler(Server &_server, bool _buckets) noexcept
		:server(_server), buckets(_buckets)
	{
		server.SetRequestHandler([this](IncomingHttpRequest &_request, CancellablePointer &_cancel_ptr) noexcept {
			HandleHttpRequest(_request, _cancel_ptr);
		});
	}

	bool IsCanceled() const noexcept {
		return canceled;
	}

	void UseRequestBody() {
		ASSERT_TRUE(request_body);

		auto &null_sink = NewNullSink(server.GetPool(), std::move(request_body),
					      BIND_THIS_METHOD(OnRequestBodyEnd));
		ReadNullSink(null_sink);
	}

	virtual void OnRequestBegin(const IncomingHttpRequest &request) noexcept = 0;
	virtual void OnRequestEnd(IncomingHttpRequest &request,
				  std::exception_ptr &&error) noexcept = 0;

private:
	void HandleHttpRequest(IncomingHttpRequest &request, CancellablePointer &cancel_ptr) noexcept {
		assert(!request_body);

		OnRequestBegin(request);

		if (!request.body) {
			OnRequestEnd(request, {});
			return;
		}

		UnusedIstreamPtr body = std::move(request.body);
		if (!buckets)
			body = NewIstreamPtr<NoBucketIstream>(request.pool, std::move(body));

		cancel_ptr = *this;
		request_ptr = &request;
		request_body = UnusedHoldIstreamPtr{request.pool, std::move(body)};
	}

	void OnRequestBodyEnd(std::exception_ptr &&error) noexcept {
		OnRequestEnd(*request_ptr, std::move(error));
	}

	void Cancel() noexcept override {
		canceled = true;
	}
};

static void
TestSimple(Server &server)
{
	server.SetRequestHandler([](IncomingHttpRequest &request, CancellablePointer &) noexcept {
		request.SendResponse(HttpStatus::OK, {},
				     istream_string_new(request.pool, "foo"));
	});

	Client client{server.GetEventLoop()};
	client.SendRequest(server,
			   HttpMethod::GET, "/", {},
			   nullptr);
	client.ExpectResponse(HttpStatus::OK, "foo"sv);
}

static void
TestMirror(Server &server)
{
	server.SetRequestHandler([](IncomingHttpRequest &request, CancellablePointer &) noexcept {
		request.SendResponse(HttpStatus::OK, {},
				     std::move(request.body));
	});

	Client client{server.GetEventLoop()};
	client.SendRequest(server,
			   HttpMethod::POST, "/", {},
			   istream_string_new(server.GetPool(), "foo"));
	client.ExpectResponse(HttpStatus::OK, "foo"sv);
}

class BufferedMirror final : GrowingBufferSinkHandler, Cancellable {
	IncomingHttpRequest &request;

	GrowingBufferSink sink;

public:
	BufferedMirror(IncomingHttpRequest &_request,
		       CancellablePointer &cancel_ptr) noexcept
		:request(_request),
		 sink(std::move(request.body), *this)
	{
		cancel_ptr = *this;
	}

private:
	void Destroy() noexcept {
		this->~BufferedMirror();
	}

	void Cancel() noexcept override {
		Destroy();
	}

	/* virtual methods from class GrowingBufferSinkHandler */
	void OnGrowingBufferSinkEof(GrowingBuffer buffer) noexcept override {
		request.SendResponse(HttpStatus::OK, {},
				     istream_gb_new(request.pool,
						    std::move(buffer)));
	}

	void OnGrowingBufferSinkError(std::exception_ptr error) noexcept override {
		const char *msg = p_strdup(request.pool,
					   GetFullMessage(error).c_str());

		request.SendResponse(HttpStatus::INTERNAL_SERVER_ERROR, {},
				     istream_string_new(request.pool, msg));
	}
};

static std::string_view
RandomString(AllocatorPtr alloc, std::size_t length) noexcept
{
	char *p = alloc.NewArray<char>(length), *q = p;
	for (std::size_t i = 0; i < length; ++i)
		*q++ = 'A' + (i % 26);
	return {p, length};
}

static void
TestBufferedMirror(Server &server)
{
	server.SetRequestHandler([](IncomingHttpRequest &request, CancellablePointer &cancel_ptr) noexcept {
		NewFromPool<BufferedMirror>(request.pool, request, cancel_ptr);
	});

	const std::string_view data = RandomString(server.GetPool(), 65536);

	Client client{server.GetEventLoop()};
	client.SendRequest(server,
			   HttpMethod::POST, "/buffered", {},
			   istream_string_new(server.GetPool(), data));
	client.ExpectResponse(HttpStatus::OK, data);
}

static UnusedIstreamPtr
MakeChunkedRequestBody(struct pool &pool) noexcept
{
	// wrap in NoLengthIstream to force chunking
	return NewIstreamPtr<NoLengthIstream>(pool, istream_string_new(pool, "X"sv));
}

/**
 * Send a chunked request body; server sends a reply after receiving
 * the whole body.  This tests whether the ABANDONED_BODY state is
 * handled correctly.
 */
template<typename F>
static void
TestChunkedRequest(HttpServerTest<F> &test, Server &server, bool buckets, bool delay_request_body)
{
	Client client{server.GetEventLoop()};

	struct Handler final : CommonHandler {
		Handler(Server &_server, bool _buckets) noexcept
			:CommonHandler(_server, _buckets) {}

		void OnRequestBegin([[maybe_unused]] const IncomingHttpRequest &request) noexcept override {
			assert(request.body);
			assert(!request.body.GetLength().exhaustive);
		}

		void OnRequestEnd(IncomingHttpRequest &request,
				  std::exception_ptr &&error) noexcept override {
                       if (error)
                               PrintException(std::move(error));

                       request.SendResponse(HttpStatus::NO_CONTENT, {}, {});
		}
	} handler{server, buckets};

	auto &pool = server.GetPool();

	auto real_request_body = MakeChunkedRequestBody(pool);

	UnusedIstreamPtr request_body;
	DelayedIstreamControl *delayed;

	if (delay_request_body) {
		auto _delayed = istream_delayed_new(pool, server.GetEventLoop());
		request_body = std::move(_delayed.first);
		delayed = &_delayed.second;
	} else
		request_body = std::move(real_request_body);

	client.SendRequest(server,
			   HttpMethod::POST, "/", {},
			   std::move(request_body));

	/* flush http_client's deferred send */
	FlushIO(server.GetEventLoop());

	if (server.HasFilter()) {
		test.FlushFilters();
		FlushIO(server.GetEventLoop());
	}

	handler.UseRequestBody();

	if (delay_request_body)
		delayed->Set(std::move(real_request_body));

	/* two calls: one receives the request body and the second one
	   detects hangup */
	FlushIO(server.GetEventLoop());
	FlushIO(server.GetEventLoop());

	if (server.HasFilter()) {
		/* the SocketFilter may require another flush */
		test.FlushFilters();
		FlushIO(server.GetEventLoop());
		FlushIO(server.GetEventLoop());
		test.FlushFilters();
		FlushIO(server.GetEventLoop());
		FlushIO(server.GetEventLoop());
	}

	EXPECT_TRUE(client.IsDone());
	client.AssertResponse(HttpStatus::NO_CONTENT, {});

	EXPECT_TRUE(!handler.IsCanceled());
}

static void
TestAbortedRequestBody(Server &server)
{
	bool request_received = false, break_request_received = false;
	server.SetRequestHandler([&](IncomingHttpRequest &request, CancellablePointer &cancel_ptr) noexcept {
		request_received = true;
		NewFromPool<BufferedMirror>(request.pool, request, cancel_ptr);

		if (break_request_received)
			server.GetEventLoop().Break();
	});

	const std::string_view data = RandomString(server.GetPool(), 65536);

	auto [inject_istream, inject_control] = istream_inject_new(server.GetPool(),
								   istream_block_new(server.GetPool()));

	Client client{server.GetEventLoop()};
	client.SendRequest(server,
			   HttpMethod::POST, "/AbortedRequestBody", {},
			   NewConcatIstream(server.GetPool(),
					    istream_string_new(server.GetPool(), data),
					    istream_head_new(server.GetPool(),
							     std::move(inject_istream),
							     32768, true)));

	if (!request_received) {
		break_request_received = true;
		server.GetEventLoop().Run();
		break_request_received = false;
		EXPECT_TRUE(request_received);
	}

	InjectFault(std::move(inject_control), std::make_exception_ptr(std::runtime_error("Inject")));
	server.WaitClosed();
}

static void
TestDiscardTinyRequestBody(Server &server)
{
	server.SetRequestHandler([](IncomingHttpRequest &request, CancellablePointer &) noexcept {
		request.body.Clear();
		request.SendResponse(HttpStatus::OK, {},
				     istream_string_new(request.pool, "foo"));
	});

	Client client{server.GetEventLoop()};
	client.SendRequest(server,
			   HttpMethod::POST, "/", {},
			   istream_string_new(server.GetPool(), "foo"));
	client.ExpectResponse(HttpStatus::OK, "foo"sv);
}

/**
 * Send a huge request body which will be discarded by the server; the
 * server then disables keepalive, sends the response and closes the
 * connection.
 */
static void
TestDiscardedHugeRequestBody(Server &server)
{
	class RespondLater {
		FineTimerEvent timer;

		IncomingHttpRequest *request;

		UnusedHoldIstreamPtr body;

	public:
		explicit RespondLater(EventLoop &event_loop) noexcept
			:timer(event_loop, BIND_THIS_METHOD(OnTimer)) {}

		void Schedule(IncomingHttpRequest &_request) noexcept {
			request = &_request;
			body = UnusedHoldIstreamPtr(request->pool, std::move(request->body));

			timer.Schedule(std::chrono::milliseconds(10));
		}

	private:
		void OnTimer() noexcept {
			body.Clear();
			request->SendResponse(HttpStatus::OK, {},
					      istream_string_new(request->pool, "foo"));
		}
	} respond_later(server.GetEventLoop());

	server.SetRequestHandler([&respond_later](IncomingHttpRequest &request, CancellablePointer &) noexcept {
		respond_later.Schedule(request);
	});

	Client client{server.GetEventLoop()};
	client.SendRequest(server,
			   HttpMethod::POST, "/", {},
			   istream_zero_new(server.GetPool()));
	client.ExpectResponse(HttpStatus::OK, "foo"sv);
}

/**
 * Send a chunked request body and cancel the request before the
 * server sends a response.
 */
template<typename F>
static void
TestCancelAfterChunkedRequest(HttpServerTest<F> &test, Server &server, bool buckets, bool delay_request_body)
{
	Client client{server.GetEventLoop()};

	struct Handler final : CommonHandler {
		Client &client;

		Handler(Server &_server, Client &_client, bool _buckets) noexcept
			:CommonHandler(_server, _buckets), client(_client) {}

		void OnRequestBegin([[maybe_unused]] const IncomingHttpRequest &request) noexcept override {
			assert(request.body);
			assert(!request.body.GetLength().exhaustive);
		}

		void OnRequestEnd([[maybe_unused]] IncomingHttpRequest &request,
				  std::exception_ptr &&error) noexcept override {
                       if (error)
                               PrintException(std::move(error));

			client.Cancel();
		}
	} handler{server, client, buckets};

	auto &pool = server.GetPool();

	// wrap in NoLengthIstream to force chunking
	auto real_request_body = NewIstreamPtr<NoLengthIstream>(pool, istream_string_new(server.GetPool(), "X"sv));

	UnusedIstreamPtr request_body;
	DelayedIstreamControl *delayed;

	if (delay_request_body) {
		auto _delayed = istream_delayed_new(pool, server.GetEventLoop());
		request_body = std::move(_delayed.first);
		delayed = &_delayed.second;
	} else
		request_body = std::move(real_request_body);

	client.SendRequest(server,
			   HttpMethod::POST, "/", {},
			   std::move(request_body));

	/* flush http_client's deferred send */
	FlushIO(server.GetEventLoop());

	if (server.HasFilter()) {
		test.FlushFilters();
		FlushIO(server.GetEventLoop());
	}

	handler.UseRequestBody();

	if (delay_request_body)
		delayed->Set(std::move(real_request_body));

	/* two calls: one receives the request body and the second one
	   detects hangup */
	FlushIO(server.GetEventLoop());
	FlushIO(server.GetEventLoop());

	if (server.HasFilter()) {
		/* the SocketFilter may require another flush */
		test.FlushFilters();
		FlushIO(server.GetEventLoop());
		FlushIO(server.GetEventLoop());
		test.FlushFilters();
		FlushIO(server.GetEventLoop());
		FlushIO(server.GetEventLoop());
	}

	EXPECT_TRUE(client.IsDone());
	client.AssertResponse(HttpStatus{}, {});

	EXPECT_TRUE(handler.IsCanceled());
}

TYPED_TEST(HttpServerTest, Misc)
{
	auto &instance = this->instance_;
	auto server = this->MakeServer();
	TestSimple(server);
	TestMirror(server);
	TestBufferedMirror(server);

	for (bool buckets : {false, true})
		for (bool delay_request_body : {false, true})
			TestChunkedRequest(*this, server, buckets, delay_request_body);

	TestDiscardTinyRequestBody(server);
	TestDiscardedHugeRequestBody(server);

	server.CloseClientSocket();
	instance.event_loop.Run();
}

TYPED_TEST(HttpServerTest, AbortedRequestBody)
{
	auto &instance = this->instance_;
	auto server = this->MakeServer();
	TestAbortedRequestBody(server);

	server.CloseClientSocket();
	instance.event_loop.Run();
}

TYPED_TEST(HttpServerTest, CancelAfterChunkedRequest)
{
	auto &instance = this->instance_;
	for (bool buckets : {false, true}) {
		for (bool delay_request_body : {false, true}) {
			auto server = this->MakeServer();
			TestCancelAfterChunkedRequest(*this, server, buckets, delay_request_body);
			server.CloseClientSocket();
			instance.event_loop.Run();
		}
	}
}
