// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "nghttp2/Server.hxx"
#include "http/Headers.hxx"
#include "http/Status.hxx"
#include "http/IncomingRequest.hxx"
#include "http/server/Handler.hxx"
#include "fs/FilteredSocket.hxx"
#include "memory/SlicePool.hxx"
#include "event/Loop.hxx"
#include "event/net/TemplateServerSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/PrintException.hxx"
#include "pool/RootPool.hxx"
#include "memory/fb_pool.hxx"

using std::string_view_literals::operator""sv;

class Connection final
	: public AutoUnlinkIntrusiveListHook,
	  HttpServerConnectionHandler, HttpServerRequestHandler
{
	NgHttp2::ServerConnection http;

public:
	Connection(struct pool &pool, EventLoop &event_loop,
		   SlicePool &request_slice_pool,
		   UniqueSocketDescriptor fd, SocketAddress address)
		:http(pool,
		      UniquePoolPtr<FilteredSocket>::Make(pool, event_loop,
							  std::move(fd), FD_TCP),
		      address,
		      request_slice_pool,
		      *this, *this) {}

	/* virtual methods from class HttpServerConnectionHandler */
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &,
			       CancellablePointer &cancel_ptr) noexcept override {
		(void)cancel_ptr;
		// TODO

		if (request.body)
			request.SendResponse(HttpStatus::OK, {},
					     std::move(request.body));
		else
			request.SendMessage(HttpStatus::OK, "Hello, world!\n"sv);
	}

	void HttpConnectionError(std::exception_ptr e) noexcept override {
		PrintException(e);
		delete this;
	}

	void HttpConnectionClosed() noexcept override {
		delete this;
	}
};

typedef TemplateServerSocket<Connection, struct pool &,
			     EventLoop &, SlicePool &> Listener;

int
main(int, char **) noexcept
try {
	const ScopeFbPoolInit fb_pool_init;
	RootPool pool;
	EventLoop event_loop;

	SlicePool request_slice_pool{8192, 256, "Requests"};
	Listener listener{event_loop, pool.get(), event_loop, request_slice_pool};
	listener.ListenTCP(8000);

	event_loop.Run();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
