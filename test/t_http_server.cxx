/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "http_server/http_server.hxx"
#include "http/IncomingRequest.hxx"
#include "http_server/Handler.hxx"
#include "http_client.hxx"
#include "http/Headers.hxx"
#include "HttpResponseHandler.hxx"
#include "lease.hxx"
#include "PInstance.hxx"
#include "pool/pool.hxx"
#include "pool/Holder.hxx"
#include "pool/UniquePtr.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ZeroIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/Sink.hxx"
#include "fb_pool.hxx"
#include "fs/FilteredSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"
#include "io/SpliceSupport.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "util/RuntimeError.hxx"
#include "stopwatch.hxx"

#include <functional>

#include <stdio.h>
#include <stdlib.h>

class Server final
	: PoolHolder, HttpServerConnectionHandler, Lease, BufferedSocketHandler
{
	HttpServerConnection *connection = nullptr;

	std::function<void(IncomingHttpRequest &request,
			   CancellablePointer &cancel_ptr)> request_handler;

	FilteredSocket client_fs;

	bool client_fs_released = false;

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

	void SendRequest(http_method_t method, const char *uri,
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

private:
	/* virtual methods from class HttpServerConnectionHandler */
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;
	void HttpConnectionError(std::exception_ptr e) noexcept override;
	void HttpConnectionClosed() noexcept override;

	/* virtual methods from class Lease */
	void ReleaseLease(bool reuse) noexcept override {
		client_fs_released = true;

		if (reuse && client_fs.IsValid() && client_fs.IsConnected()) {
			client_fs.Reinit(Event::Duration(-1), Event::Duration(-1), *this);
			client_fs.UnscheduleWrite();
		} else {
			CloseClientSocket();
		}
	}

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override {
		fprintf(stderr, "unexpected data in idle TCP connection");
		CloseClientSocket();
		return BufferedResult::CLOSED;
	}

	bool OnBufferedClosed() noexcept override {
		CloseClientSocket();
		return false;
	}

	gcc_noreturn
	bool OnBufferedWrite() override {
		/* should never be reached because we never schedule
		   writing */
		gcc_unreachable();
	}

	void OnBufferedError(std::exception_ptr e) noexcept override {
		PrintException(e);
		CloseClientSocket();
	}
};

class Client final : HttpResponseHandler, IstreamSink {
	CancellablePointer client_cancel_ptr;

	std::exception_ptr response_error;
	std::string response_body;
	http_status_t status{};

	bool response_eof = false;

public:
	void SendRequest(Server &server,
			 http_method_t method, const char *uri,
			 const StringMap &headers,
			 UnusedIstreamPtr body, bool expect_100=false) noexcept {
		server.SendRequest(method, uri, headers,
				   std::move(body), expect_100,
				   *this, client_cancel_ptr);
	}

	bool IsClientDone() const noexcept {
		return response_error || response_eof;
	}

	void WaitDone(Server &server) {
		auto &event_loop = server.GetEventLoop();
		while (!IsClientDone())
			event_loop.LoopOnce();
	}

	void RethrowResponseError() const {
		if (response_error)
			std::rethrow_exception(response_error);
	}

	void ExpectResponse(Server &server, http_status_t expected_status,
			    const char *expected_body) {
		WaitDone(server);
		RethrowResponseError();

		if (status != expected_status)
			throw FormatRuntimeError("Got status %d, expected %d\n",
						 int(status), int(expected_status));

		if (response_body != expected_body)
			throw FormatRuntimeError("Got response body '%s', expected '%s'\n",
						 response_body.c_str(), expected_body);
	}

private:
	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(http_status_t _status, StringMap &&headers,
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

	size_t OnData(const void *data, size_t length) noexcept override {
		response_body.append((const char *)data, length);
		return length;
	}

	void OnEof() noexcept override {
		IstreamSink::ClearInput();
		response_eof = true;
	}

	void OnError(std::exception_ptr ep) noexcept override {
		IstreamSink::ClearInput();
		response_error = std::move(ep);
	}
};

Server::Server(struct pool &_pool, EventLoop &event_loop)
	:PoolHolder(pool_new_libc(&_pool, "catch")),
	 client_fs(event_loop)
{
	UniqueSocketDescriptor client_socket, server_socket;
	if (!UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
						      client_socket, server_socket))
		throw MakeErrno("socketpair() failed");

	connection = http_server_connection_new(pool,
						UniquePoolPtr<FilteredSocket>::Make(pool,
										    event_loop,
										    std::move(server_socket),
										    FdType::FD_SOCKET),
						nullptr, nullptr,
						true, *this);

	client_fs.InitDummy(client_socket.Release(), FdType::FD_SOCKET);
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
}

void
Server::HttpConnectionClosed() noexcept
{
	connection = nullptr;
}

static void
TestSimple(Server &server)
{
	server.SetRequestHandler([](IncomingHttpRequest &request, CancellablePointer &) noexcept {
		request.SendResponse(HTTP_STATUS_OK, {},
				     istream_string_new(request.pool, "foo"));
	});

	Client client;
	client.SendRequest(server,
			   HTTP_METHOD_GET, "/", {},
			   nullptr);
	client.ExpectResponse(server, HTTP_STATUS_OK, "foo");
}

static void
TestMirror(Server &server)
{
	server.SetRequestHandler([](IncomingHttpRequest &request, CancellablePointer &) noexcept {
		request.SendResponse(HTTP_STATUS_OK, {},
				     std::move(request.body));
	});

	Client client;
	client.SendRequest(server,
			   HTTP_METHOD_POST, "/", {},
			   istream_string_new(server.GetPool(), "foo"));
	client.ExpectResponse(server, HTTP_STATUS_OK, "foo");
}

static void
TestDiscardTinyRequestBody(Server &server)
{
	server.SetRequestHandler([](IncomingHttpRequest &request, CancellablePointer &) noexcept {
		request.body.Clear();
		request.SendResponse(HTTP_STATUS_OK, {},
				     istream_string_new(request.pool, "foo"));
	});

	Client client;
	client.SendRequest(server,
			   HTTP_METHOD_POST, "/", {},
			   istream_string_new(server.GetPool(), "foo"));
	client.ExpectResponse(server, HTTP_STATUS_OK, "foo");
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
		TimerEvent timer;

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
			request->SendResponse(HTTP_STATUS_OK, {},
					      istream_string_new(request->pool, "foo"));
		}
	} respond_later(server.GetEventLoop());

	server.SetRequestHandler([&respond_later](IncomingHttpRequest &request, CancellablePointer &) noexcept {
		respond_later.Schedule(request);
	});

	Client client;
	client.SendRequest(server,
			   HTTP_METHOD_POST, "/", {},
			   istream_zero_new(server.GetPool()));
	client.ExpectResponse(server, HTTP_STATUS_OK, "foo");
}

int
main(int argc, char **argv) noexcept
try {
	(void)argc;
	(void)argv;

	direct_global_init();
	const ScopeFbPoolInit fb_pool_init;
	PInstance instance;

	{
		Server server(instance.root_pool, instance.event_loop);
		TestSimple(server);
		TestMirror(server);
		TestDiscardTinyRequestBody(server);
		TestDiscardedHugeRequestBody(server);

		server.CloseClientSocket();
		instance.event_loop.Dispatch();
	}
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
