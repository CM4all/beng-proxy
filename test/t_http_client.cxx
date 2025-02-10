// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "t_client.hxx"
#include "DemoHttpServerConnection.hxx"
#include "http/Client.hxx"
#include "http/Headers.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketPair.hxx"
#include "system/Error.hxx"
#include "fs/Factory.hxx"
#include "fs/FilteredSocket.hxx"
#include "fs/NopSocketFilter.hxx"
#include "fs/NopThreadSocketFilter.hxx"
#include "fs/ThreadSocketFilter.hxx"
#include "thread/Pool.hxx"
#include "pool/UniquePtr.hxx"
#include "istream/New.hxx"
#include "istream/DeferReadIstream.hxx"
#include "istream/PipeLeaseIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "stopwatch.hxx"
#include "util/AbortFlag.hxx"

#include <functional>
#include <memory>
#include <thread>

#include <sys/socket.h>

using std::string_view_literals::operator""sv;

class Server final : DemoHttpServerConnection {
public:
	using DemoHttpServerConnection::DemoHttpServerConnection;

	static auto New(struct pool &pool, EventLoop &event_loop, Mode mode) {
		auto [client_socket, server_socket] = CreateStreamSocketPair();

		auto server = std::make_unique<Server>(pool, event_loop,
						       UniquePoolPtr<FilteredSocket>::Make(pool,
											   event_loop,
											   std::move(server_socket),
											   FdType::FD_SOCKET),
						       nullptr,
						       mode);
		return std::make_pair(std::move(server), std::move(client_socket));
	}
};

class HttpClientConnection final : public ClientConnection {
	std::thread thread;

	std::unique_ptr<Server> server;

	FilteredSocket socket;

	const std::string peer_name{"localhost"};

public:
	HttpClientConnection(EventLoop &_event_loop,
			     std::thread &&_thread,
			     UniqueSocketDescriptor fd,
			     SocketFilterPtr _filter) noexcept
		:thread(std::move(_thread)),
		 socket(_event_loop)
	{
		socket.InitDummy(std::move(fd), FdType::FD_SOCKET,
				 std::move(_filter));
	}

	HttpClientConnection(EventLoop &_event_loop,
			     std::pair<std::unique_ptr<Server>, UniqueSocketDescriptor> _server,
			     SocketFilterPtr _filter)
		:server(std::move(_server.first)),
		 socket(_event_loop,
			std::move(_server.second), FdType::FD_SOCKET,
			std::move(_filter))
	{
	}

	~HttpClientConnection() noexcept override;

	void Request(struct pool &pool,
		     Lease &lease,
		     HttpMethod method, const char *uri,
		     StringMap &&headers,
		     UnusedIstreamPtr body,
		     bool expect_100,
		     HttpResponseHandler &handler,
		     CancellablePointer &cancel_ptr) noexcept override {
		http_client_request(pool, nullptr,
				    socket, lease,
				    peer_name.c_str(),
				    method, uri, headers, {},
				    std::move(body), expect_100,
				    handler, cancel_ptr);
	}

	void InjectSocketFailure() noexcept override {
		socket.Shutdown();
	}
};

struct HttpClientFactory {
	using Error = HttpClientError;
	using ErrorCode = HttpClientErrorCode;

	static constexpr const ClientTestOptions options{
		.have_chunked_request_body = true,
		.have_expect_100 = true,
		.enable_buckets = true,
		.enable_close_ignored_request_body = true,
	};

	SocketFilterFactoryPtr socket_filter_factory;

	HttpClientFactory(SocketFilterFactoryPtr &&_socket_filter_factory) noexcept
		:socket_filter_factory(std::move(_socket_filter_factory)) {}

	SocketFilterPtr CreateFilter() {
		return socket_filter_factory
			? socket_filter_factory->CreateFilter()
			: nullptr;
	}

	/**
	 * Create a HTTP connection to a new child process acting as a
	 * HTTP server.
	 */
	HttpClientConnection *NewFork(EventLoop &event_loop,
				      std::function<void(SocketDescriptor)> function);

	/**
	 * Create a HTTP connection to a new child process which
	 * writes the specified string as HTTP response.
	 */
	HttpClientConnection *NewForkWrite(EventLoop &event_loop,
					   std::string_view response);

	auto *NewWithServer(struct pool &pool,
			    EventLoop &event_loop,
			    DemoHttpServerConnection::Mode mode) noexcept {
		return new HttpClientConnection(event_loop,
						Server::New(pool, event_loop, mode),
						CreateFilter());
	}

	auto *NewMirror(struct pool &pool, EventLoop &event_loop) noexcept {
		return NewWithServer(pool, event_loop,
				     DemoHttpServerConnection::Mode::MIRROR);
	}

	auto *NewDeferMirror(struct pool &pool, EventLoop &event_loop) noexcept {
		return NewWithServer(pool, event_loop,
				     DemoHttpServerConnection::Mode::DEFER_MIRROR);
	}

	auto *NewNull(struct pool &pool, EventLoop &event_loop) noexcept {
		return NewWithServer(pool, event_loop,
				     DemoHttpServerConnection::Mode::MODE_NULL);
	}

	auto *NewDummy(struct pool &pool, EventLoop &event_loop) noexcept {
		return NewWithServer(pool, event_loop,
				     DemoHttpServerConnection::Mode::DUMMY);
	}

	auto *NewClose(struct pool &pool, EventLoop &event_loop) noexcept {
		return NewWithServer(pool, event_loop,
				     DemoHttpServerConnection::Mode::CLOSE);
	}

	auto *NewFixed(struct pool &pool, EventLoop &event_loop) noexcept {
		return NewWithServer(pool, event_loop,
				     DemoHttpServerConnection::Mode::FIXED);
	}

	auto *NewTiny(struct pool &p, EventLoop &event_loop) noexcept {
		return NewFixed(p, event_loop);
	}

	auto *NewHuge(struct pool &pool, EventLoop &event_loop) noexcept {
		return NewWithServer(pool, event_loop,
				     DemoHttpServerConnection::Mode::HUGE_);
	}

	auto *NewTwice100(struct pool &, EventLoop &event_loop) {
		return NewForkWrite(event_loop, "HTTP/1.1 100 Continue\r\n\r\n"
				    "HTTP/1.1 100 Continue\r\n\r\n"
				    "HTTP/1.1 200 OK\r\n\r\n"sv);
	}

	auto *NewClose100(struct pool &, EventLoop &event_loop) {
		return NewForkWrite(event_loop, "HTTP/1.1 100 Continue\n\n"sv);
	}

	auto *NewManySmallChunks(struct pool &, EventLoop &event_loop) {
		return NewForkWrite(event_loop, "HTTP/1.1 200 OK\r\n"
				    "transfer-encoding: chunked\r\n"
				    "\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "1\r\na\r\n"
				    "0\r\n\r\n"sv);
	}


	auto *NewHold(struct pool &pool, EventLoop &event_loop) noexcept {
		return NewWithServer(pool, event_loop,
				     DemoHttpServerConnection::Mode::HOLD);
	}

	auto *NewBlock(struct pool &pool,
		       EventLoop &event_loop) noexcept {
		return NewWithServer(pool, event_loop,
				     DemoHttpServerConnection::Mode::BLOCK);
	}

	auto *NewNop(struct pool &pool, EventLoop &event_loop) noexcept {
		return NewWithServer(pool, event_loop,
				     DemoHttpServerConnection::Mode::NOP);
	}

	auto *NewIgnoredRequestBody(struct pool &, EventLoop &event_loop) {
		return NewForkWrite(event_loop, "HTTP/1.1 200 OK\r\n"
				    "Content-Length: 3\r\n"
				    "\r\nfoo"sv);
	}
};

HttpClientConnection::~HttpClientConnection() noexcept
{
	// TODO code copied from ~FilteredSocket()
	if (socket.IsValid()) {
		if (socket.IsConnected())
			socket.Close();
		socket.Destroy();
	}

	if (thread.joinable())
		thread.join();
}

HttpClientConnection *
HttpClientFactory::NewFork(EventLoop &event_loop,
			   std::function<void(SocketDescriptor)> _function)
{
	auto [client_socket, server_socket] = CreateStreamSocketPair();

	// not using std::thread because libc++ still doesn't have it in 2023
	std::thread thread{[](UniqueSocketDescriptor s, std::function<void(SocketDescriptor)> function){
		function(s);
	}, std::move(server_socket), std::move(_function)};

	client_socket.SetNonBlocking();
	return new HttpClientConnection(event_loop, std::move(thread),
					std::move(client_socket),
					CreateFilter());
}

HttpClientConnection *
HttpClientFactory::NewForkWrite(EventLoop &event_loop, std::string_view response)
{
	return NewFork(event_loop, [response](SocketDescriptor s){
		/* wait until the request becomes ready */
		s.WaitReadable(-1);

		(void)s.Send(AsBytes(response));
		s.ShutdownWrite();

		std::byte buffer[64];
		do {
			s.WaitReadable(-1);
		} while (s.ReadNoWait(buffer) > 0);
	});
}

template<typename T>
class HttpClientTest : public ::testing::Test {
};

TYPED_TEST_SUITE_P(HttpClientTest);

/**
 * Keep-alive disabled, and response body has unknown length, ends
 * when server closes socket.  Check if our HTTP client handles such
 * responses correctly.
 */
TYPED_TEST_P(HttpClientTest, NoKeepalive)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewClose(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);
	pool_commit();

	c.WaitForResponse();

	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.request_error, nullptr);

	/* receive the rest of the response body from the buffer */
	c.event_loop.Run();

	EXPECT_TRUE(c.released);
	EXPECT_TRUE(c.body_eof);
	EXPECT_TRUE(c.body_data > 0);
	EXPECT_EQ(c.body_error, nullptr);
}

/**
 * The server ignores the request body, and sends the whole response
 * (keep-alive enabled).  The HTTP client's response body handler
 * blocks, and then more request body data becomes available.  This
 * used to trigger an assertion failure, because the HTTP client
 * forgot about the in-progress request body.
 */
TYPED_TEST_P(HttpClientTest, IgnoredRequestBody)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	auto delayed = istream_delayed_new(*c.pool, c.event_loop);
	AbortFlag abort_flag(delayed.second.cancel_ptr);
	auto zero = istream_zero_new(*c.pool);

	c.data_blocking = 1;
	c.connection = factory.NewIgnoredRequestBody(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/ignored-request-body", {},
			      std::move(delayed.first),
			      false,
			      c, c.cancel_ptr);

	c.WaitForEnd();

	/* at this point, the HTTP client must have closed the request
	   body; but if it has not due to the bug, this will trigger
	   the assertion failure: */
	if (!abort_flag.aborted) {
		delayed.second.Set(std::move(zero));
		c.event_loop.Run();
	}

	EXPECT_TRUE(abort_flag.aborted);

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.consumed_body_data, 3);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_EQ(c.lease_action, PutAction::DESTROY);
}

static char *
RandomString(AllocatorPtr alloc, std::size_t length) noexcept
{
	char *p = alloc.NewArray<char>(length + 1), *q = p;
	for (std::size_t i = 0; i < length; ++i)
		*q++ = 'A' + (i % 26);
	*q = 0;
	return p;
}

static PipeLease
FillPipeLease(struct pool &pool, PipeStock *stock,
	      std::size_t length)
{
	PipeLease pl(stock);
	pl.Create();

	char *data = RandomString(pool, length);
	auto nbytes = pl.GetWriteFd().Write(std::as_bytes(std::span{data, length}));
	if (nbytes < 0)
		throw MakeErrno("Failed to write to pipe");

	if (std::size_t(nbytes) < length)
		throw std::runtime_error("Short write to pipe");

	return pl;
}

static UnusedIstreamPtr
FillPipeLeaseIstream(struct pool &pool, PipeStock *stock,
		     std::size_t length)
{
	return NewIstreamPtr<PipeLeaseIstream>(pool,
					       FillPipeLease(pool, stock,
							     length),
					       length);
}

/**
 * Send a request with "Expect: 100-continue" with a request body that
 * can be spliced.
 */
TYPED_TEST_P(HttpClientTest, Expect100ContinueSplice)
{
	constexpr std::size_t length = 4096;

	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.connection = factory.NewDeferMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/expect_100_continue_splice",
			      {},
			      NewIstreamPtr<DeferReadIstream>(*c.pool, c.event_loop,
							      FillPipeLeaseIstream(*c.pool, nullptr, length)),
			      true,
			      c, c.cancel_ptr);

	c.WaitForEnd();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.consumed_body_data, length);
	EXPECT_EQ(c.body_error, nullptr);
	EXPECT_EQ(c.lease_action, PutAction::REUSE);
}

/**
 * Parse a response with many small chunks.  The whole response fits
 * into the input buffer, but the DechunkIstream did not fully analyze
 * it, and that led to an assertion failure.
 */
TYPED_TEST_P(HttpClientTest, ManySmallChunks)
{
	Instance instance;
	TypeParam factory{instance.event_loop};
	Context c{instance};

	c.use_buckets = true;
	c.connection = factory.NewManySmallChunks(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/",
			      {}, {},
			      true,
			      c, c.cancel_ptr);

	c.WaitForEnd();

	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.connection, nullptr);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.consumed_body_data, 16);
	EXPECT_EQ(c.body_error, nullptr);
}

REGISTER_TYPED_TEST_SUITE_P(HttpClientTest,
			    NoKeepalive,
			    IgnoredRequestBody,
			    Expect100ContinueSplice,
			    ManySmallChunks);

class NopSocketFilterFactory final : public SocketFilterFactory {
public:
	SocketFilterPtr CreateFilter() override {
		return SocketFilterPtr{new NopSocketFilter()};
	}
};

class NopThreadSocketFilterFactory final : public SocketFilterFactory {
	EventLoop &event_loop;

public:
	explicit NopThreadSocketFilterFactory(EventLoop &_event_loop) noexcept
		:event_loop(_event_loop) {
		/* keep the eventfd unregistered if the ThreadQueue is
		   empty, so EventLoop::Dispatch() doesn't keep
		   running after the HTTP request has completed */
		thread_pool_set_volatile();
	}

	~NopThreadSocketFilterFactory() noexcept override {
		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	SocketFilterPtr CreateFilter() override {
		return SocketFilterPtr{
			new ThreadSocketFilter(thread_pool_get_queue(event_loop),
					       std::make_unique<NopThreadSocketFilter>())
		};
	}
};

class NullHttpClientFactory final : public HttpClientFactory {
public:
	explicit NullHttpClientFactory(EventLoop &) noexcept
		:HttpClientFactory(nullptr) {}
};

INSTANTIATE_TYPED_TEST_SUITE_P(HttpClient, ClientTest, NullHttpClientFactory);
INSTANTIATE_TYPED_TEST_SUITE_P(HttpClient, HttpClientTest, NullHttpClientFactory);

class NopHttpClientFactory final : public HttpClientFactory {
public:
	explicit NopHttpClientFactory(EventLoop &) noexcept
		:HttpClientFactory(std::make_unique<NopSocketFilterFactory>()) {}
};

INSTANTIATE_TYPED_TEST_SUITE_P(HttpClientNop, ClientTest, NopHttpClientFactory);
INSTANTIATE_TYPED_TEST_SUITE_P(HttpClientNop, HttpClientTest, NopHttpClientFactory);

class NopThreadHttpClientFactory final : public HttpClientFactory {
public:
	explicit NopThreadHttpClientFactory(EventLoop &event_loop) noexcept
		:HttpClientFactory(std::make_unique<NopThreadSocketFilterFactory>(event_loop)) {}
};

INSTANTIATE_TYPED_TEST_SUITE_P(HttpClientNopThread, ClientTest, NopThreadHttpClientFactory);
INSTANTIATE_TYPED_TEST_SUITE_P(HttpClientNopThread, HttpClientTest, NopThreadHttpClientFactory);
