// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "t_client.hxx"
#include "fcgi/Client.hxx"
#include "system/SetupProcess.hxx"
#include "io/Pipe.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "lease.hxx"
#include "istream/UnusedPtr.hxx"
#include "strmap.hxx"
#include "event/net/BufferedSocket.hxx"
#include "net/SocketPair.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/ByteOrder.hxx"
#include "util/PrintException.hxx"
#include "util/SpanCast.hxx"
#include "fcgi_server.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest-param-test.h>

#include <functional>
#include <thread>

using std::string_view_literals::operator""sv;

static void
fcgi_server_mirror(struct pool &pool, FcgiServer &server)
{
	auto request = server.ReadRequest(pool);

	HttpStatus status = request.length == 0
		? HttpStatus::NO_CONTENT
		: HttpStatus::OK;

	char buffer[32];
	if (request.length > 0) {
		sprintf(buffer, "%llu", (unsigned long long)request.length);
		request.headers.Add(pool, content_length_header, buffer);
	}

	server.WriteResponseHeaders(request, status, request.headers);

	if (request.method == HttpMethod::HEAD)
		server.DiscardRequestBody(request);
	else {
		while (true) {
			server.FlushOutput();
			auto header = server.ReadHeader();

			if (header.type != FcgiRecordType::STDIN ||
			    header.request_id != request.id)
				abort();

			if (header.content_length == 0)
				break;

			header.type = FcgiRecordType::STDOUT;
			server.WriteHeader(header);
			server.MirrorRaw(header.content_length + header.padding_length);
		}
	}

	server.EndResponse(request);
}

static void
fcgi_server_null(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);
	server.WriteResponseHeaders(request, HttpStatus::NO_CONTENT, {});
	server.EndResponse(request);
	server.FlushOutput();
	server.DiscardRequestBody(request);
}

static void
fcgi_server_hello(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);

	server.WriteResponseHeaders(request, HttpStatus::OK, {});
	server.DiscardRequestBody(request);
	server.WriteStdout(request, "hello"sv, 42);

	/* writing a STDERR packet, trying to confuse the client */
	server.WriteStderr(request, "err\n"sv, 13);

	/* some more confusion: an unknown record which should be
	   ignored by the client */
	server.WriteRecord(request, FcgiRecordType::UNKNOWN_TYPE, "ignore this"sv, 7);

	server.EndResponse(request);
}

static void
fcgi_server_tiny(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);

	server.DiscardRequestBody(request);
	server.WriteStdout(request, "content-length: 5\n\nhello"sv);
	server.EndResponse(request);
}

static void
fcgi_server_malformed_header_name(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);

	server.DiscardRequestBody(request);
	server.WriteStdout(request, "header name: foo\n\nhello"sv);
	server.EndResponse(request);
}

static void
fcgi_server_malformed_header_value(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);

	server.DiscardRequestBody(request);
	server.WriteStdout(request, "header: foo\rbar\n\nhello"sv);
	server.EndResponse(request);
}

static void
fcgi_server_huge(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);

	server.DiscardRequestBody(request);
	server.WriteStdout(request, "content-length: 524288\n\n"sv);

	char buffer[23456];
	memset(buffer, 0xab, sizeof(buffer));

	size_t remaining = 524288;
	while (remaining > 0) {
		size_t nbytes = std::min(remaining, sizeof(buffer));
		server.WriteStdout(request, {buffer, nbytes});
		remaining -= nbytes;
	}

	server.EndResponse(request);
}

[[noreturn]]
static void
fcgi_server_hold(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);
	server.WriteResponseHeaders(request, HttpStatus::OK, {});
	server.FlushOutput();

	/* wait until the connection gets closed */
	while (true) {
		server.ReadHeader();
	}
}

static void
fcgi_server_premature_close_headers(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);
	server.DiscardRequestBody(request);

	server.WriteHeader({
		.version = FCGI_VERSION_1,
		.type = FcgiRecordType::STDOUT,
		.request_id = request.id,
		.content_length = 1024,
	});

	server.WriteFullRaw(AsBytes("Foo: 1\nBar: 1\nX: "sv));
}

static void
fcgi_server_premature_close_body(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);
	server.DiscardRequestBody(request);

	server.WriteHeader({
		.version = FCGI_VERSION_1,
		.type = FcgiRecordType::STDOUT,
		.request_id = request.id,
		.content_length = 1024,
	});

	server.WriteFullRaw(AsBytes("Foo: 1\nBar: 1\n\nFoo Bar"sv));
}

static void
fcgi_server_premature_end(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);

	server.DiscardRequestBody(request);
	server.WriteStdout(request, "content-length: 524288\n\nhello"sv);
	server.EndResponse(request);
}

static void
fcgi_server_excess_data(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);

	server.DiscardRequestBody(request);
	server.WriteStdout(request, "content-length: 5\n\nhello world"sv);
	server.EndResponse(request);
}

static void
fcgi_server_nop(struct pool &pool, FcgiServer &server)
{
	const auto request = server.ReadRequest(pool);
	server.DiscardRequestBody(request);
}

class FcgiClientConnection final : public ClientConnection {
	EventLoop &event_loop;
	std::thread thread;
	BufferedSocket socket;

	UniqueFileDescriptor stderr_w;

public:
	FcgiClientConnection(EventLoop &_event_loop, std::thread &&_thread,
			     UniqueSocketDescriptor &&_fd)
		:event_loop(_event_loop),
		 thread(std::move(_thread)),
		 socket(event_loop)
	{
		socket.Init(_fd.Release(), FdType::FD_SOCKET);
	}

	~FcgiClientConnection() noexcept override {
		socket.Close();

		if (thread.joinable())
			thread.join();
	}

	void SetStderr(UniqueFileDescriptor &&_stderr_w) noexcept {
		stderr_w = std::move(_stderr_w);
	}

	void Request(struct pool &pool,
		     Lease &lease,
		     HttpMethod method, const char *uri,
		     StringMap &&headers, UnusedIstreamPtr body,
		     [[maybe_unused]] bool expect_100,
		     HttpResponseHandler &handler,
		     CancellablePointer &cancel_ptr) noexcept override {
		fcgi_client_request(&pool, nullptr,
				    socket, lease,
				    method, uri, uri, nullptr, nullptr, nullptr,
				    nullptr, "192.168.1.100",
				    std::move(headers), std::move(body),
				    {},
				    std::move(stderr_w),
				    handler, cancel_ptr);
	}

	void InjectSocketFailure() noexcept override {
		socket.GetSocket().Shutdown();
	}
};

struct FcgiClientFactory {
	static constexpr ClientTestOptions options{
		.can_cancel_request_body = true,
		.have_content_length_header = false,
		.enable_buckets = true,
		.enable_premature_close_headers = true,
		.enable_premature_close_body = true,
		.enable_premature_end = true,
		.enable_excess_data = true,
	};

	explicit FcgiClientFactory(EventLoop &) noexcept {}

	~FcgiClientFactory() noexcept {
		int status;
		while (wait(&status) > 0) {
			assert(!WIFSIGNALED(status));
		}
	}

	using ServerFunction = std::function<void(struct pool &pool, FcgiServer &server)>;

	static FcgiClientConnection *New(EventLoop &event_loop, ServerFunction function);

	auto *NewMirror(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_mirror);
	}

	auto *NewNull(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_null);
	}

	auto *NewDummy(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_hello);
	}

	auto *NewFixed(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_hello);
	}

	auto *NewTiny(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_tiny);
	}

	auto *NewMalformedHeaderName(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_malformed_header_name);
	}

	auto *NewMalformedHeaderValue(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_malformed_header_value);
	}

	auto *NewHuge(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_huge);
	}

	auto *NewHold(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_hold);
	}

	auto *NewBlock(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_hold);
	}

	auto *NewPrematureCloseHeaders(struct pool &,
				       EventLoop &event_loop) {
		return New(event_loop, fcgi_server_premature_close_headers);
	}

	auto *NewPrematureCloseBody(struct pool &,
				    EventLoop &event_loop) {
		return New(event_loop, fcgi_server_premature_close_body);
	}

	auto *NewPrematureEnd(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_premature_end);
	}

	auto *NewExcessData(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_excess_data);
	}

	auto *NewNop(struct pool &, EventLoop &event_loop) {
		return New(event_loop, fcgi_server_nop);
	}

	auto *NewInterleavedStderr(struct pool &, EventLoop &event_loop) {
		return New(event_loop, [](struct pool &pool, FcgiServer &server){
			const auto request = server.ReadRequest(pool);

			server.DiscardRequestBody(request);
			server.WriteStdout(request, "content-length: 5\n\nhel"sv, 3);

			server.WriteStderr(request, "foobar\n", 13);

			server.WriteStdout(request, "lo"sv, 7);
			server.EndResponse(request);
		});
	}

	/**
	 * Like NewInterleavedStderr(), but the server blocks in the
	 * middle of the STDERR payload until a pipe becomes readable.
	 */
	auto *NewBlockingStderr(EventLoop &event_loop,
				UniqueFileDescriptor &&wait_pipe_r) {
		return New(event_loop, [&wait_pipe_r](struct pool &pool, FcgiServer &server){
			const auto request = server.ReadRequest(pool);

			server.DiscardRequestBody(request);
			server.WriteStdout(request, "content-length: 5\n\nhel"sv, 3);

			server.WriteHeader({
				.version = FCGI_VERSION_1,
				.type = FcgiRecordType::STDERR,
				.request_id = request.id,
				.content_length = 7,
			});

			server.WriteFullRaw(AsBytes("foo"sv));
			server.FlushOutput();

			wait_pipe_r.WaitReadable(-1);
			server.WriteFullRaw(AsBytes("bar\n"sv));

			server.WriteStdout(request, "lo"sv, 7);
			server.EndResponse(request);
		});
	}

	auto *NewIncompleteEndRequest(struct pool &, EventLoop &event_loop) {
		return New(event_loop, [](struct pool &pool, FcgiServer &server){
			const auto request = server.ReadRequest(pool);
			server.DiscardRequestBody(request);
			server.WriteStdout(request, "content-length: 5\n\nhello"sv);
			server.WriteHeader({
					.version = FCGI_VERSION_1,
					.type = FcgiRecordType::END_REQUEST,
					.request_id = request.id,
					.padding_length = 1,
				});
		});
	}

	/**
	 * The server blocks after the last STDOUT and sends
	 * END_REQUEST later.
	 */
	auto *NewBlockingEnd(EventLoop &event_loop,
			     UniqueFileDescriptor &&wait_pipe_r) {
		return New(event_loop, [&wait_pipe_r](struct pool &pool, FcgiServer &server){
			const auto request = server.ReadRequest(pool);

			server.DiscardRequestBody(request);
			server.WriteStdout(request, "content-length: 5\n\nhello"sv, 3);
			server.FlushOutput();
			wait_pipe_r.WaitReadable(-1);
			server.WriteStderr(request, "foobar\n", 13);
			server.EndResponse(request);
		});
	}
};

FcgiClientConnection *
FcgiClientFactory::New(EventLoop &event_loop, ServerFunction _function)
{
	auto [server_socket, client_socket] = CreateStreamSocketPair();

	// not using std::thread because libc++ still doesn't have it in 2023
	std::thread thread{[](UniqueSocketDescriptor s, ServerFunction function){
		auto pool = pool_new_libc(nullptr, "f");
		FcgiServer server{std::move(s)};

		try {
			function(*pool, server);
			server.FlushOutput();
		} catch (...) {
			PrintException(std::current_exception());
		}

		server.Shutdown();
		pool.reset();
	}, std::move(server_socket), std::move(_function)};

	client_socket.SetNonBlocking();
	return new FcgiClientConnection(event_loop, std::move(thread),
					std::move(client_socket));
}

INSTANTIATE_TYPED_TEST_SUITE_P(FcgiClient, ClientTest, FcgiClientFactory);

TEST(FcgiClient, MalformedHeaderName)
{
	Instance instance;
	FcgiClientFactory factory{instance.event_loop};
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

TEST(FcgiClient, MalformedHeaderValue)
{
	Instance instance;
	FcgiClientFactory factory{instance.event_loop};
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

class FcgiClientB : public testing::TestWithParam<bool> {};

static std::string
ReadStderr(FileDescriptor fd) noexcept
{
	char buffer[4096];
	auto nbytes = fd.Read(std::as_writable_bytes(std::span{buffer}));
	if (nbytes <= 0)
		return {};
	return {buffer, static_cast<std::size_t>(nbytes)};
}

/**
 * A STDERR packet between two STDOUT.  Let's see if that confuses the
 * FastCGI client.
 */
TEST_P(FcgiClientB, InterleavedStderr)
{
	Instance instance;
	FcgiClientFactory factory{instance.event_loop};
	Context c{instance};

	c.use_buckets = GetParam();

	auto [stderr_r, stderr_w] = CreatePipeNonBlock();

	auto *connection = factory.NewInterleavedStderr(*c.pool, c.event_loop);
	connection->SetStderr(std::move(stderr_w));

	c.connection = connection;
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_FALSE(c.request_error);
	EXPECT_FALSE(c.body_error);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.available, 5);
	EXPECT_EQ(c.body_data, 5);
	EXPECT_EQ(c.consumed_body_data, 5);
	EXPECT_TRUE(c.body_eof);
	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.lease_action, PutAction::REUSE);
	EXPECT_EQ(ReadStderr(stderr_r), "foobar\n"sv);
}

/**
 * Server sends an incomplete END_REQUEST which should cause an error
 * at the end of the response body.
 */
TEST_P(FcgiClientB, IncompleteEndRequest)
{
	Instance instance;
	FcgiClientFactory factory{instance.event_loop};
	Context c{instance};

	c.use_buckets = GetParam();

	c.connection = factory.NewIncompleteEndRequest(*c.pool, c.event_loop);
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,

			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_TRUE(c.request_error || c.body_error);
	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.lease_action, PutAction::DESTROY);
}

/**
 * The server blocks after the last STDOUT and sends END_REQUEST
 * later.
 */
TEST_P(FcgiClientB, BlockingEnd)
{
	Instance instance;
	FcgiClientFactory factory{instance.event_loop};
	Context c{instance};

	c.use_buckets = GetParam();
	c.break_data = true;

	auto [wait_pipe_r, wait_pipe_w] = CreatePipe();
	auto [stderr_r, stderr_w] = CreatePipeNonBlock();

	auto  *connection = factory.NewBlockingEnd(c.event_loop,
						   std::move(wait_pipe_r));
	connection->SetStderr(std::move(stderr_w));

	c.connection = connection;
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_FALSE(c.request_error);
	EXPECT_FALSE(c.body_error);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.available, 5);
	EXPECT_EQ(c.body_data, 5);
	EXPECT_EQ(c.consumed_body_data, 5);
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.released);
	EXPECT_EQ(ReadStderr(stderr_r), ""sv);

	c.break_data = false;

	wait_pipe_w.Close();
	c.event_loop.Run();

	EXPECT_FALSE(c.request_error);
	EXPECT_FALSE(c.body_error);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.available, 5);
	EXPECT_EQ(c.body_data, 5);
	EXPECT_EQ(c.consumed_body_data, 5);
	EXPECT_TRUE(c.body_eof);
	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.lease_action, PutAction::REUSE);
	EXPECT_EQ(ReadStderr(stderr_r), "foobar\n"sv);
}

/**
 * The server blocks in the middle of the STDERR payload, and after
 * that, we switch to buckets.
 */
TEST_P(FcgiClientB, BlockingStderr)
{
	Instance instance;
	FcgiClientFactory factory{instance.event_loop};
	Context c{instance};

	c.use_buckets = GetParam();
	c.break_data = true;

	auto [wait_pipe_r, wait_pipe_w] = CreatePipe();
	auto [stderr_r, stderr_w] = CreatePipeNonBlock();

	auto *connection = factory.NewBlockingStderr(c.event_loop,
						     std::move(wait_pipe_r));
	connection->SetStderr(std::move(stderr_w));

	c.connection = connection;
	c.connection->Request(c.pool, c,
			      HttpMethod::GET, "/foo", {},
			      nullptr,
			      false,
			      c, c.cancel_ptr);

	c.event_loop.Run();

	EXPECT_FALSE(c.request_error);
	EXPECT_FALSE(c.body_error);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.available, 5);
	EXPECT_EQ(c.body_data, 3);
	EXPECT_EQ(c.consumed_body_data, 3);
	EXPECT_FALSE(c.body_eof);
	EXPECT_FALSE(c.released);
	EXPECT_EQ(ReadStderr(stderr_r), "foo"sv);

	c.break_data = false;
	c.use_buckets = true;

	wait_pipe_w.Close();
	c.event_loop.Run();

	EXPECT_FALSE(c.request_error);
	EXPECT_FALSE(c.body_error);
	EXPECT_EQ(c.status, HttpStatus::OK);
	EXPECT_EQ(c.available, 5);
	EXPECT_EQ(c.body_data, 5);
	EXPECT_EQ(c.consumed_body_data, 5);
	EXPECT_TRUE(c.body_eof);
	EXPECT_TRUE(c.released);
	EXPECT_EQ(c.lease_action, PutAction::REUSE);
	EXPECT_EQ(ReadStderr(stderr_r), "bar\n"sv);
}

INSTANTIATE_TEST_SUITE_P(FcgiClient,
                         FcgiClientB,
                         testing::Values(false, true));
