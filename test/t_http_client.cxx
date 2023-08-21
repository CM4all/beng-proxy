// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "t_client.hxx"
#include "DemoHttpServerConnection.hxx"
#include "http/Client.hxx"
#include "http/Headers.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "system/Error.hxx"
#include "fs/Factory.hxx"
#include "fs/FilteredSocket.hxx"
#include "fs/NopSocketFilter.hxx"
#include "fs/NopThreadSocketFilter.hxx"
#include "fs/ThreadSocketFilter.hxx"
#include "thread/Pool.hxx"
#include "memory/fb_pool.hxx"
#include "pool/UniquePtr.hxx"
#include "istream/New.hxx"
#include "istream/DeferReadIstream.hxx"
#include "istream/PipeLeaseIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "stopwatch.hxx"
#include "util/AbortFlag.hxx"

#include <functional>
#include <memory>

#include <sys/socket.h>
#include <sys/wait.h>

using std::string_view_literals::operator""sv;

class Server final : DemoHttpServerConnection {
public:
	using DemoHttpServerConnection::DemoHttpServerConnection;

	static auto New(struct pool &pool, EventLoop &event_loop, Mode mode) {
		UniqueSocketDescriptor client_socket, server_socket;
		if (!UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
							      client_socket, server_socket))
			throw MakeErrno("socketpair() failed");

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
	const pid_t pid = 0;

	std::unique_ptr<Server> server;

	FilteredSocket socket;

	const std::string peer_name{"localhost"};

public:
	HttpClientConnection(EventLoop &_event_loop, pid_t _pid,
			     SocketDescriptor fd,
			     SocketFilterPtr _filter) noexcept
		:pid(_pid), socket(_event_loop) {
		socket.InitDummy(fd, FdType::FD_SOCKET,
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

	if (pid > 0) {
		int status;
		if (waitpid(pid, &status, 0) < 0) {
			perror("waitpid() failed");
			exit(EXIT_FAILURE);
		}

		assert(!WIFSIGNALED(status));
	}
}

HttpClientConnection *
HttpClientFactory::NewFork(EventLoop &event_loop,
			   std::function<void(SocketDescriptor)> function)
{
	SocketDescriptor client_socket, server_socket;
	if (!SocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
						client_socket, server_socket)) {
		perror("socketpair() failed");
		exit(EXIT_FAILURE);
	}

	const auto pid = fork();
	if (pid < 0) {
		perror("fork() failed");
		exit(EXIT_FAILURE);
	}

	if (pid == 0) {
		client_socket.Close();
		function(server_socket);
		_exit(EXIT_SUCCESS);
	}

	server_socket.Close();
	client_socket.SetNonBlocking();
	return new HttpClientConnection(event_loop, pid, client_socket,
					CreateFilter());
}

HttpClientConnection *
HttpClientFactory::NewForkWrite(EventLoop &event_loop, std::string_view response)
{
	return NewFork(event_loop, [response](SocketDescriptor s){
		/* wait until the request becomes ready */
		s.WaitReadable(-1);

		(void)s.Write(response.data(), response.size());
		s.ShutdownWrite();

		char buffer[64];
		do {
			s.WaitReadable(-1);
		} while (s.Read(buffer, sizeof(buffer)) > 0);
	});
}

/**
 * Keep-alive disabled, and response body has unknown length, ends
 * when server closes socket.  Check if our HTTP client handles such
 * responses correctly.
 */
static void
test_no_keepalive(auto &factory, Context &c) noexcept
{
	c.connection = factory.NewClose(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);
	pool_commit();

	c.WaitForResponse();

	assert(c.status == HttpStatus::OK);
	assert(c.request_error == nullptr);

	/* receive the rest of the response body from the buffer */
	c.event_loop.Run();

	assert(c.released);
	assert(c.body_eof);
	assert(c.body_data > 0);
	assert(c.body_error == nullptr);
}

/**
 * The server ignores the request body, and sends the whole response
 * (keep-alive enabled).  The HTTP client's response body handler
 * blocks, and then more request body data becomes available.  This
 * used to trigger an assertion failure, because the HTTP client
 * forgot about the in-progress request body.
 */
static void
test_ignored_request_body(auto &factory, Context &c) noexcept
{
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

	assert(abort_flag.aborted);

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::OK);
	assert(c.consumed_body_data == 3);
	assert(c.body_error == nullptr);
	assert(!c.reuse);
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
	auto nbytes = pl.GetWriteFd().Write(data, length);
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
static void
test_expect_100_continue_splice(auto &factory, Context &c) noexcept
{
	constexpr std::size_t length = 4096;

	c.connection = factory.NewDeferMirror(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::POST, "/expect_100_continue_splice",
			      {},
			      NewIstreamPtr<DeferReadIstream>(*c.pool, c.event_loop,
							      FillPipeLeaseIstream(*c.pool, nullptr, length)),
			      true,
			      c, c.cancel_ptr);

	c.WaitForEnd();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::OK);
	assert(c.consumed_body_data == length);
	assert(c.body_error == nullptr);
	assert(c.reuse);
}

/**
 * Parse a response with many small chunks.  The whole response fits
 * into the input buffer, but the DechunkIstream did not fully analyze
 * it, and that led to an assertion failure.
 */
static void
test_many_small_chunks(auto &factory, Context &c) noexcept
{
	c.use_buckets = true;
	c.connection = factory.NewManySmallChunks(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/",
			      {}, {},
			      true,
			      c, c.cancel_ptr);

	c.WaitForEnd();

	assert(c.released);
	assert(c.connection == nullptr);
	assert(c.status == HttpStatus::OK);
	assert(c.consumed_body_data == 16);
	assert(c.body_error == nullptr);
	assert(!c.reuse);
}

/*
 * main
 *
 */

static void
RunHttpClientTests(Instance &instance, SocketFilterFactoryPtr &&socket_filter_factory) noexcept
{
	HttpClientFactory factory{std::move(socket_filter_factory)};

	run_all_tests(instance, factory);
	run_test(instance, factory, test_no_keepalive);
	run_test(instance, factory, test_ignored_request_body);
	run_test(instance, factory, test_expect_100_continue_splice);
	run_test(instance, factory, test_many_small_chunks);
}

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

	~NopThreadSocketFilterFactory() noexcept {
		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	SocketFilterPtr CreateFilter() override {
		return SocketFilterPtr{
			new ThreadSocketFilter(event_loop,
					       thread_pool_get_queue(event_loop),
					       std::make_unique<NopThreadSocketFilter>())
		};
	}
};

int
main(int, char **)
{
	SetupProcess();

	direct_global_init();
	const ScopeFbPoolInit fb_pool_init;

	Instance instance;

	RunHttpClientTests(instance, nullptr);
	RunHttpClientTests(instance, std::make_unique<NopSocketFilterFactory>());
	RunHttpClientTests(instance, std::make_unique<NopThreadSocketFilterFactory>(instance.event_loop));
}
