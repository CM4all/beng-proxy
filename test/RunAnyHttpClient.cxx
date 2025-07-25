// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "cluster/AddressListBuilder.hxx"
#include "http/Address.hxx"
#include "http/AnyClient.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"
#include "istream/OpenFileIstream.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "istream/istream.hxx"
#include "istream/sink_fd.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "pool/PSocketAddress.hxx"
#include "fs/FilteredSocket.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "ssl/Init.hxx"
#include "ssl/Client.hxx"
#include "ssl/Config.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "net/FailureManager.hxx"
#include "net/HostParser.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "event/ShutdownListener.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"

#ifdef HAVE_NGHTTP2
#include "nghttp2/Stock.hxx"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct Context final
	: TestInstance,
	  SinkFdHandler,
	  HttpResponseHandler {

	ShutdownListener shutdown_listener{
		event_loop,
		BIND_THIS_METHOD(ShutdownCallback),
	};

	const ScopeSslGlobalInit ssl_init;
	SslClientFactory ssl_client_factory{SslClientConfig{}};

	FailureManager failure_manager;

	FilteredSocketStock fs_stock{event_loop, 1, 1};

	FilteredSocketBalancer fs_balancer{fs_stock, failure_manager};

#ifdef HAVE_NGHTTP2
	NgHttp2::Stock nghttp2_stock;
#endif

	AnyHttpClient any_client{
		fs_balancer,
#ifdef HAVE_NGHTTP2
		nghttp2_stock,
#endif
		&ssl_client_factory,
	};

	PoolPtr pool;

	CancellablePointer cancel_ptr;

	UniqueSocketDescriptor fd;
	FilteredSocket fs;

	bool idle, aborted, got_response = false;
	HttpStatus status;

	SinkFd *body = nullptr;
	bool body_eof, body_abort;

	Context()
		:shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
		 pool(pool_new_linear(root_pool, "test", 8192)),
		 fs(event_loop) {}

	void Quit() noexcept {
#ifdef HAVE_NGHTTP2
		nghttp2_stock.FadeAll();
#endif

		fs_stock.FadeAll();

		shutdown_listener.Disable();
	}

	void ShutdownCallback() noexcept;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class SinkFdHandler */
	void OnInputEof() noexcept override;
	void OnInputError(std::exception_ptr ep) noexcept override;
	bool OnSendError(int error) noexcept override;
};

void
Context::ShutdownCallback() noexcept
{
	if (body != nullptr) {
		sink_fd_close(body);
		body = nullptr;
		body_abort = true;
	} else {
		aborted = true;
		cancel_ptr.Cancel();
	}

	Quit();
}

void
Context::OnInputEof() noexcept
{
	body = nullptr;
	body_eof = true;

	Quit();
}

void
Context::OnInputError(std::exception_ptr ep) noexcept
{
	PrintException(ep);

	body = nullptr;
	body_abort = true;

	Quit();
}

bool
Context::OnSendError(int error) noexcept
{
	fprintf(stderr, "%s\n", strerror(error));

	body = nullptr;
	body_abort = true;

	Quit();
	return true;
}

void
Context::OnHttpResponse(HttpStatus _status, StringMap &&,
			UnusedIstreamPtr _body) noexcept
{
	got_response = true;
	status = _status;

	if (_body) {
		body = sink_fd_new(event_loop, *pool,
				   NewAutoPipeIstream(pool, std::move(_body), nullptr),
				   FileDescriptor(STDOUT_FILENO),
				   guess_fd_type(STDOUT_FILENO),
				   *this);
		sink_fd_read(body);
	} else {
		body_eof = true;
		Quit();
	}
}

void
Context::OnHttpError(std::exception_ptr ep) noexcept
{
	PrintException(ep);

	aborted = true;

	Quit();
}

int
main(int argc, char **argv)
try {
	Context ctx;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: run_client URL [BODY]\n");
		return EXIT_FAILURE;
	}

	auto *address = http_address_parse(*ctx.pool, argv[1]);

	if (address->host_and_port != nullptr) {
		static constexpr struct addrinfo hints{
			.ai_flags = AI_ADDRCONFIG,
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
		};

		AddressListBuilder address_list_builder;
		address_list_builder.Add(*ctx.pool,
					 Resolve(address->host_and_port,
						 address->ssl ? 443 : 80,
						 &hints));

		address->addresses = address_list_builder.Finish(*ctx.pool);
	}

	address->Check();

	SetupProcess();

	/* initialize */

	ctx.shutdown_listener.Enable();

	/* open request body */

	HttpMethod method = HttpMethod::GET;
	UnusedIstreamPtr request_body;

	if (argc >= 3) {
		method = HttpMethod::POST;
		request_body = OpenFileIstream(ctx.event_loop, ctx.pool,
					       argv[2]);
	}

	/* connect */

	ctx.any_client.SendRequest(ctx.pool, {}, 0,
				   method, *address,
				   {}, std::move(request_body),
				   ctx, ctx.cancel_ptr);

	/* run test */

	ctx.event_loop.Run();

	assert(!ctx.got_response || ctx.body_eof || ctx.body_abort || ctx.aborted);

	/* cleanup */

	ctx.pool.reset();
	pool_commit();

	return ctx.got_response && ctx.body_eof ? EXIT_SUCCESS : EXIT_FAILURE;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
