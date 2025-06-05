// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TestInstance.hxx"
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
#include "istream/InjectIstream.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ZeroIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/Sink.hxx"
#include "memory/GrowingBuffer.hxx"
#include "memory/SinkGrowingBuffer.hxx"
#include "memory/SlicePool.hxx"
#include "memory/istream_gb.hxx"
#include "fs/FilteredSocket.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "event/FineTimerEvent.hxx"
#include "net/SocketPair.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

#include <functional>
#include <utility> // for std::unreachable()

#include <stdio.h>
#include <stdlib.h>

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

	bool client_fs_released = false;

	bool break_closed = false;

public:
	Server(struct pool &_pool, EventLoop &event_loop);

	~Server() noexcept {
		CloseClientSocket();
		CheckCloseConnection();
	}

	using PoolHolder::GetPool;

	auto &GetEventLoop() noexcept {
		return client_fs.GetEventLoop();
	}

	template<typename T>
	void SetRequestHandler(T &&handler) noexcept {
		request_handler = std::forward<T>(handler);
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
		if (client_fs.IsValid() && client_fs.IsConnected()) {
			client_fs.Close();
			client_fs.Destroy();
		}
	}

	void WaitClosed() noexcept {
		if (connection == nullptr)
			return;

		break_closed = true;
		GetEventLoop().Run();
		break_closed = false;

		assert(connection == nullptr);
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

class Client final : HttpResponseHandler, IstreamSink {
	EventLoop &event_loop;

	CancellablePointer client_cancel_ptr;

	std::exception_ptr response_error;
	std::string response_body;
	HttpStatus status{};

	bool response_eof = false;

	bool break_done = false;

public:
	explicit Client(EventLoop &_event_loop) noexcept
		:event_loop(_event_loop) {}

	void SendRequest(Server &server,
			 HttpMethod method, const char *uri,
			 const StringMap &headers,
			 UnusedIstreamPtr body, bool expect_100=false) noexcept {
		server.SendRequest(method, uri, headers,
				   std::move(body), expect_100,
				   *this, client_cancel_ptr);
	}

	bool IsClientDone() const noexcept {
		return response_error || response_eof;
	}

	void WaitDone() noexcept {
		if (IsClientDone())
			return;

		break_done = true;
		event_loop.Run();
		break_done = false;

		assert(IsClientDone());
	}

	void RethrowResponseError() const {
		if (response_error)
			std::rethrow_exception(response_error);
	}

	void ExpectResponse(HttpStatus expected_status,
			    const char *expected_body) {
		WaitDone();
		RethrowResponseError();

		if (status != expected_status)
			throw FmtRuntimeError("Got status {}, expected {}\n",
					      int(status), int(expected_status));

		if (response_body != expected_body)
			throw FmtRuntimeError("Got response body '{}', expected '{}'",
					      response_body, expected_body);
	}

private:
	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus _status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override {
		status = _status;

		(void)headers;

		IstreamSink::SetInput(std::move(body));
		input.Read();
	}

	void OnHttpError(std::exception_ptr ep) noexcept override {
		response_error = std::move(ep);
	}

	/* virtual methods from class IstreamHandler */

	size_t OnData(std::span<const std::byte> src) noexcept override {
		response_body.append(ToStringView(src));
		return src.size();
	}

	void OnEof() noexcept override {
		IstreamSink::ClearInput();
		response_eof = true;

		if (break_done)
			event_loop.Break();
	}

	void OnError(std::exception_ptr ep) noexcept override {
		IstreamSink::ClearInput();
		response_error = std::move(ep);

		if (break_done)
			event_loop.Break();
	}
};

Server::Server(struct pool &_pool, EventLoop &event_loop)
	:PoolHolder(pool_new_libc(&_pool, "catch")),
	 client_fs(event_loop)
{
	auto [client_socket, server_socket] = CreateStreamSocketPair();

	connection = http_server_connection_new(pool,
						UniquePoolPtr<FilteredSocket>::Make(pool,
										    event_loop,
										    std::move(server_socket),
										    FdType::FD_SOCKET),
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
	client.ExpectResponse(HttpStatus::OK, "foo");
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
	client.ExpectResponse(HttpStatus::OK, "foo");
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

static char *
RandomString(AllocatorPtr alloc, std::size_t length) noexcept
{
	char *p = alloc.NewArray<char>(length + 1), *q = p;
	for (std::size_t i = 0; i < length; ++i)
		*q++ = 'A' + (i % 26);
	*q = 0;
	return p;
}

static void
TestBufferedMirror(Server &server)
{
	server.SetRequestHandler([](IncomingHttpRequest &request, CancellablePointer &cancel_ptr) noexcept {
		NewFromPool<BufferedMirror>(request.pool, request, cancel_ptr);
	});

	char *data = RandomString(server.GetPool(), 65536);

	Client client{server.GetEventLoop()};
	client.SendRequest(server,
			   HttpMethod::POST, "/buffered", {},
			   istream_string_new(server.GetPool(), data));
	client.ExpectResponse(HttpStatus::OK, data);
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

	char *data = RandomString(server.GetPool(), 65536);

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
		assert(request_received);
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
	client.ExpectResponse(HttpStatus::OK, "foo");
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
	client.ExpectResponse(HttpStatus::OK, "foo");
}

int
main(int argc, char **argv) noexcept
try {
	(void)argc;
	(void)argv;

	TestInstance instance;

	{
		Server server(instance.root_pool, instance.event_loop);
		TestSimple(server);
		TestMirror(server);
		TestBufferedMirror(server);
		TestDiscardTinyRequestBody(server);
		TestDiscardedHugeRequestBody(server);

		server.CloseClientSocket();
		instance.event_loop.Run();
	}

	{
		Server server(instance.root_pool, instance.event_loop);
		TestAbortedRequestBody(server);

		server.CloseClientSocket();
		instance.event_loop.Run();
	}
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
