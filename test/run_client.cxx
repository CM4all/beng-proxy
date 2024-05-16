// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TestInstance.hxx"
#include "strmap.hxx"
#include "http/Client.hxx"
#include "http/CommonHeaders.hxx"
#include "http/Headers.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"
#include "lease.hxx"
#include "istream/OpenFileIstream.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "istream/istream.hxx"
#include "istream/sink_fd.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "fs/FilteredSocket.hxx"
#include "ssl/Init.hxx"
#include "ssl/Client.hxx"
#include "ssl/Config.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "net/HostParser.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "event/net/ConnectSocket.hxx"
#include "event/ShutdownListener.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

#ifdef HAVE_NGHTTP2
#include "nghttp2/Client.hxx"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

struct parsed_url {
	enum {
		HTTP,
#ifdef HAVE_NGHTTP2
		HTTP2,
#endif
	} protocol;

	bool ssl = false;

	std::string host;

	int default_port;

	const char *uri;
};

static struct parsed_url
parse_url(const char *url)
{
	assert(url != nullptr);

	struct parsed_url dest;

	if (memcmp(url, "http://", 7) == 0) {
		url += 7;
		dest.protocol = parsed_url::HTTP;
		dest.default_port = 80;
	} else if (memcmp(url, "https://", 8) == 0) {
		url += 8;
		dest.protocol = parsed_url::HTTP;
		dest.ssl = true;
		dest.default_port = 443;
#ifdef HAVE_NGHTTP2
	} else if (memcmp(url, "http2://", 8) == 0) {
		url += 8;
		dest.protocol = parsed_url::HTTP2;
		dest.default_port = 80;
	} else if (memcmp(url, "https2://", 8) == 0) {
		url += 9;
		dest.protocol = parsed_url::HTTP2;
		dest.ssl = true;
		dest.default_port = 443;
#endif
	} else
		throw std::runtime_error("Unsupported URL");

	dest.uri = strchr(url, '/');
	if (dest.uri == nullptr || dest.uri == url)
		throw std::runtime_error("Missing URI path");

	dest.host = std::string(url, dest.uri);

	return dest;
}

[[gnu::pure]]
static const char *
GetHostWithoutPort(struct pool &pool, const struct parsed_url &url) noexcept
{
	if (url.host.empty())
		return nullptr;

	auto e = ExtractHost(url.host.c_str());
	if (e.host.data() == nullptr)
		return nullptr;

	return p_strdup(pool, e.host);
}

struct Context final
	: TestInstance, ConnectSocketHandler, Lease,
#ifdef HAVE_NGHTTP2
	  NgHttp2::ConnectionHandler,
#endif
	  SinkFdHandler,
	  HttpResponseHandler {

	struct parsed_url url;

	ShutdownListener shutdown_listener;

	PoolPtr pool;

	const ScopeSslGlobalInit ssl_init;
	SslClientFactory ssl_client_factory{SslClientConfig{}};

#ifdef HAVE_NGHTTP2
	std::unique_ptr<NgHttp2::ClientConnection> nghttp2_client;
#endif

	CancellablePointer cancel_ptr;

	HttpMethod method;
	UnusedIstreamPtr request_body;

	UniqueSocketDescriptor fd;
	FilteredSocket fs;

	PutAction lease_action;
	bool idle, aborted, got_response = false;
	HttpStatus status;

	SinkFd *body = nullptr;
	bool body_eof, body_abort;

	Context()
		:shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)),
		 pool(pool_new_linear(root_pool, "test", 8192)),
		 fs(event_loop) {}

	void ShutdownCallback() noexcept;

	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept override;
	void OnSocketConnectError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction _action) noexcept override {
		assert(!idle);
		assert(url.protocol == parsed_url::HTTP ||
		       fd.IsDefined());

		idle = true;
		lease_action = _action;

		if (url.protocol == parsed_url::HTTP) {
			if (fs.IsConnected())
				fs.Close();
			fs.Destroy();
		} else
			fd.Close();

		return PutAction::DESTROY;
	}

#ifdef HAVE_NGHTTP2
	/* virtual methods from class NgHttp2::ConnectionHandler */
	void OnNgHttp2ConnectionIdle() noexcept override;
	void OnNgHttp2ConnectionError(std::exception_ptr e) noexcept override;
	void OnNgHttp2ConnectionClosed() noexcept override;
#endif

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

	shutdown_listener.Disable();
}

/*
 * istream handler
 *
 */

void
Context::OnInputEof() noexcept
{
	body = nullptr;
	body_eof = true;

	shutdown_listener.Disable();
}

void
Context::OnInputError(std::exception_ptr ep) noexcept
{
	PrintException(ep);

	body = nullptr;
	body_abort = true;

	shutdown_listener.Disable();
}

bool
Context::OnSendError(int error) noexcept
{
	fprintf(stderr, "%s\n", strerror(error));

	body = nullptr;
	body_abort = true;

	shutdown_listener.Disable();
	return true;
}

#ifdef HAVE_NGHTTP2

void
Context::OnNgHttp2ConnectionIdle() noexcept
{
	nghttp2_client.reset();
}

void
Context::OnNgHttp2ConnectionError(std::exception_ptr e) noexcept
{
	PrintException(e);
	nghttp2_client.reset();
}

void
Context::OnNgHttp2ConnectionClosed() noexcept
{
	// TODO
	nghttp2_client.reset();
}

#endif

/*
 * http_response_handler
 *
 */

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
		shutdown_listener.Disable();
	}
}

void
Context::OnHttpError(std::exception_ptr ep) noexcept
{
	PrintException(ep);

	aborted = true;

	shutdown_listener.Disable();
}


/*
 * client_socket_handler
 *
 */

void
Context::OnSocketConnectSuccess(UniqueSocketDescriptor new_fd) noexcept
try {
	fd = std::move(new_fd);
	idle = false;

	StringMap headers;
	headers.Add(*pool, host_header, url.host.c_str());

	SocketFilterPtr socket_filter;
	if (url.ssl) {
		SslClientAlpn alpn = SslClientAlpn::NONE;
		switch (url.protocol) {
		case parsed_url::HTTP:
			break;

#ifdef HAVE_NGHTTP2
		case parsed_url::HTTP2:
			alpn = SslClientAlpn::HTTP_2;
			break;
#endif
		}

		socket_filter = ssl_client_factory.Create(event_loop,
							  GetHostWithoutPort(*pool, url),
							  nullptr, alpn);
	}

	std::unique_ptr<FilteredSocket> fsp;

	switch (url.protocol) {
	case parsed_url::HTTP:
		fs.InitDummy(fd.Release(), FdType::FD_TCP,
			     std::move(socket_filter));

		http_client_request(*pool, nullptr, fs,
				    *this,
				    "localhost",
				    method, url.uri,
				    headers, {},
				    std::move(request_body), false,
				    *this,
				    cancel_ptr);
		break;

#ifdef HAVE_NGHTTP2
	case parsed_url::HTTP2:
		lease_action = PutAction::DESTROY;

		fsp = std::make_unique<FilteredSocket>(event_loop,
						       std::move(fd), FdType::FD_TCP,
						       std::move(socket_filter));

		nghttp2_client = std::make_unique<NgHttp2::ClientConnection>(std::move(fsp),
									     *this);

		nghttp2_client->SendRequest(*pool, nullptr,
					    method, url.uri,
					    std::move(headers),
					    std::move(request_body),
					    *this, cancel_ptr);
		break;
#endif
	}
} catch (const std::runtime_error &e) {
	PrintException(e);

	aborted = true;
	request_body.Clear();

	shutdown_listener.Disable();
}

void
Context::OnSocketConnectError(std::exception_ptr ep) noexcept
{
	PrintException(ep);

	aborted = true;
	request_body.Clear();

	shutdown_listener.Disable();
}

/*
 * main
 *
 */

int
main(int argc, char **argv)
try {
	Context ctx;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: run_client URL [BODY]\n");
		return EXIT_FAILURE;
	}

	ctx.url = parse_url(argv[1]);

	SetupProcess();

	/* connect socket */

	static constexpr auto hints = MakeAddrInfo(AI_ADDRCONFIG, AF_UNSPEC,
						   SOCK_STREAM);

	const auto ail = Resolve(ctx.url.host.c_str(), ctx.url.default_port,
				 &hints);
	const auto &ai = ail.front();

	/* initialize */

	ctx.shutdown_listener.Enable();

	/* open request body */

	if (argc >= 3) {
		ctx.method = HttpMethod::POST;

		ctx.request_body = OpenFileIstream(ctx.event_loop, ctx.pool,
						   argv[2]);
	} else {
		ctx.method = HttpMethod::GET;
	}

	/* connect */

	ConnectSocket connect(ctx.event_loop, ctx);
	ctx.cancel_ptr = connect;
	connect.Connect(ai, std::chrono::seconds(30));

	/* run test */

	ctx.event_loop.Run();

	assert(!ctx.got_response || ctx.body_eof || ctx.body_abort || ctx.aborted);

	if (ctx.got_response)
		fprintf(stderr, "reuse=%d\n", ctx.lease_action == PutAction::REUSE);

	/* cleanup */

	ctx.pool.reset();
	pool_commit();

	return ctx.got_response && ctx.body_eof ? EXIT_SUCCESS : EXIT_FAILURE;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
