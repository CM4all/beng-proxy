// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ForwardHttpRequest.hxx"
#include "RLogger.hxx"
#include "HttpConnection.hxx"
#include "Cluster.hxx"
#include "ClusterConfig.hxx"
#include "Instance.hxx"
#include "Session.hxx"
#include "Cookie.hxx"
#include "JvmRoute.hxx"
#include "Headers.hxx"
#include "cluster/AddressSticky.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Client.hxx"
#include "fs/Handler.hxx"
#include "http/CommonHeaders.hxx"
#include "http/ResponseHandler.hxx"
#include "http/Headers.hxx"
#include "http/Method.hxx"
#include "strmap.hxx"
#include "pool/pool.hxx"
#include "net/IPv4Address.hxx"
#include "net/IPv6Address.hxx"
#include "net/SocketAddress.hxx"
#include "net/FailureManager.hxx"
#include "net/FailureRef.hxx"
#include "net/PToString.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "util/Cancellable.hxx"
#include "util/CharUtil.hxx"
#include "util/LeakDetector.hxx"
#include "util/FNVHash.hxx"
#include "util/StringCompare.hxx"
#include "util/StringVerify.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

#include <algorithm> // for std::copy_n()

static constexpr Event::Duration LB_HTTP_CONNECT_TIMEOUT =
	std::chrono::seconds{10};

class LbRequest final
	: LeakDetector, Cancellable, FilteredSocketBalancerHandler, HttpResponseHandler {

	struct pool &pool;

	LbHttpConnection &connection;
	LbCluster &cluster;
	const LbClusterConfig &cluster_config;

	IncomingHttpRequest &request;

	/**
	 * The request body.
	 */
	UnusedHoldIstreamPtr body;

	CancellablePointer cancel_ptr;

	FailurePtr failure;

	unsigned new_cookie = 0;

public:
	LbRequest(LbHttpConnection &_connection, LbCluster &_cluster,
		  IncomingHttpRequest &_request,
		  CancellablePointer &_cancel_ptr) noexcept
		:pool(_request.pool), connection(_connection), cluster(_cluster),
		 cluster_config(cluster.GetConfig()),
		 request(_request),
		 body(pool, std::move(request.body)) {
		_cancel_ptr = *this;
	}

	EventLoop &GetEventLoop() const noexcept {
		return connection.instance.event_loop;
	}

	FailureManager &GetFailureManager() const noexcept {
		return connection.instance.failure_manager;
	}

	void Start() noexcept;

private:
	void Destroy() noexcept {
		DeleteFromPool(pool, this);
	}

	void SetForwardedTo() noexcept {
		assert(failure);

		auto &rl = *(LbRequestLogger *)request.logger;
		rl.forwarded_to = GetFailureManager().GetAddressString(*failure);
	}

	const char *GetCanonicalHost() const noexcept {
		auto &rl = *(LbRequestLogger *)request.logger;
		return rl.GetCanonicalHost();
	}

	/**
	 * Returns a pointer to the data section that will be used to
	 * calculate the sticky hash.  May return a null span if the
	 * current sticky mode does not support this.
	 */
	[[gnu::pure]]
	std::span<const std::byte> GetStickySource() const noexcept;

	sticky_hash_t GetStickyHash() noexcept;
	sticky_hash_t GetHostHash() const noexcept;
	sticky_hash_t GetXHostHash() const noexcept;
	sticky_hash_t MakeCookieHash() noexcept;

	uint_fast64_t MakeFairnessHash() const noexcept;

	SocketAddress MakeBindAddress() const noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		connection.RecordAbuse();
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class FilteredSocketBalancerHandler */
	void OnFilteredSocketReady(Lease &lease,
				   FilteredSocket &socket,
				   SocketAddress address, const char *name,
				   ReferencedFailureInfo &failure) noexcept override;
	void OnFilteredSocketError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

static bool
send_fallback(IncomingHttpRequest &request,
	      const LbSimpleHttpResponse &fallback) noexcept
{
	if (!fallback.IsDefined())
		return false;

	SendResponse(request, fallback);
	return true;
}

inline sticky_hash_t
LbRequest::GetHostHash() const noexcept
{
	const char *host = GetCanonicalHost();
	if (host == nullptr)
		return 0;

	return FNV1aHash32(host);
}

inline sticky_hash_t
LbRequest::GetXHostHash() const noexcept
{
	const char *host = request.headers.Get(x_cm4all_host_header);
	if (host == nullptr)
		return 0;

	return FNV1aHash32(host);
}

/**
 * Generate a cookie for sticky worker selection.  Return only worker
 * numbers that are not known to be failing.  Returns 0 on total
 * failure.
 */
static unsigned
GenerateCookie(const FailureManager &failure_manager, Expiry now,
	       const AddressList &list) noexcept
{
	assert(list.size() >= 2);

	const unsigned first = lb_cookie_generate(list.size());

	unsigned i = first;
	do {
		assert(i >= 1 && i <= list.size());
		const SocketAddress address = list.addresses[i % list.size()];
		if (failure_manager.Check(now, address))
			return i;

		i = lb_cookie_next(list.size(), i);
	} while (i != first);

	/* all nodes have failed */
	return first;
}

sticky_hash_t
LbRequest::MakeCookieHash() noexcept
{
	unsigned hash = lb_cookie_get(request.headers);
	if (hash == 0)
		new_cookie = hash = GenerateCookie(GetFailureManager(),
						   GetEventLoop().SteadyNow(),
						   cluster_config.address_list);

	return hash;
}

sticky_hash_t
LbRequest::GetStickyHash() noexcept
{
	switch (cluster_config.sticky_mode) {
	case StickyMode::NONE:
	case StickyMode::FAILOVER:
		/* these modes require no preparation; they are handled
		   completely by balancer_get() */
		break;

	case StickyMode::SOURCE_IP:
		/* calculate the sticky hash from remote address */
		return socket_address_sticky(request.remote_address);

	case StickyMode::HOST:
		/* calculate the sticky hash from "Host" request header */
		return GetHostHash();

	case StickyMode::XHOST:
		/* calculate the sticky hash from "X-CM4all-Host" request
		   header */
		return GetXHostHash();

	case StickyMode::SESSION_MODULO:
		/* calculate the sticky hash from beng-proxy session id */
		return lb_session_get(request.headers,
				      cluster_config.session_cookie.c_str());

	case StickyMode::COOKIE:
		/* calculate the sticky hash from beng-lb cookie */
		return MakeCookieHash();

	case StickyMode::JVM_ROUTE:
		/* calculate the sticky hash from JSESSIONID cookie suffix */
		return lb_jvm_route_get(request.headers, cluster_config);
	}

	return 0;
}

inline std::span<const std::byte>
LbRequest::GetStickySource() const noexcept
{
	if (!cluster_config.sticky_hex_uuid_uri_prefix.empty()) {
		if (const char *s = StringAfterPrefix(request.uri, cluster_config.sticky_hex_uuid_uri_prefix)) {
			static constexpr std::size_t HEX_DIGITS = 32;
			static constexpr std::size_t UUID_LENGTH = 36;
			const std::string_view sv{s};

			// TODO throw "400 Bad Request" on malformed UUID

			if (sv.size() >= UUID_LENGTH &&
			    CheckChars(sv.substr(0, 8), IsLowerHexDigit) &&
			    sv[8] == '-' &&
			    CheckChars(sv.substr(9, 4), IsLowerHexDigit) &&
			    sv[13] == '-' &&
			    CheckChars(sv.substr(14, 4), IsLowerHexDigit) &&
			    sv[18] == '-' &&
			    CheckChars(sv.substr(19, 4), IsLowerHexDigit) &&
			    sv[23] == '-' &&
			    CheckChars(sv.substr(24, 12), IsLowerHexDigit)) {
				/* it's already a well-formed UUID
				   (with hyphens) */
				return AsBytes(sv.substr(0, UUID_LENGTH));
			}

			if (sv.size() >= HEX_DIGITS && CheckChars(sv.substr(0, HEX_DIGITS), IsLowerHexDigit)) {
				/* there are 32 hex digits in the URI,
				   but to make it a UUID string, we
				   need to insert four dashes */
				const std::span<char, UUID_LENGTH> uuid{PoolAlloc<char>(pool, UUID_LENGTH), UUID_LENGTH};
				char *p = uuid.data();
				p = std::copy_n(s, 8, p);
				*p++ = '-';
				p = std::copy_n(s + 8, 4, p);
				*p++ = '-';
				p = std::copy_n(s + 12, 4, p);
				*p++ = '-';
				p = std::copy_n(s + 16, 4, p);
				*p++ = '-';
				p = std::copy_n(s + 20, 12, p);
				assert(p == uuid.data() + UUID_LENGTH);
				return std::as_bytes(uuid);
			}
		}
	}

	switch (cluster_config.sticky_mode) {
	case StickyMode::NONE:
	case StickyMode::FAILOVER:
		/* these modes require no preparation; they are handled
		   completely by balancer_get() */
		break;

	case StickyMode::SOURCE_IP:
		/* calculate the sticky hash from remote address */
		return request.remote_address.GetSteadyPart();

	case StickyMode::HOST:
		/* calculate the sticky hash from "Host" request header */
		if (const char *host = GetCanonicalHost())
			return AsBytes(std::string_view{host});

		break;

	case StickyMode::XHOST:
		/* calculate the sticky hash from "X-CM4all-Host" request
		   header */
		if (const char *host = request.headers.Get(x_cm4all_host_header))
			return AsBytes(std::string_view{host});

		break;

	case StickyMode::SESSION_MODULO:
	case StickyMode::COOKIE:
	case StickyMode::JVM_ROUTE:
		// this mode is not supported by this method
		break;
	}

	return {};
}

/*
 * HTTP response handler
 *
 */

void
LbRequest::OnHttpResponse(HttpStatus status, StringMap &&_headers,
			  UnusedIstreamPtr response_body) noexcept
{
	failure->UnsetProtocol();

	if (auto &rl = *(LbRequestLogger *)request.logger; rl.generator == nullptr)
		/* if there is a GENERATOR header, include it in the
		   access log */
		/* we remove the header here because usually the
		   client isn't interested; but what if we have
		   chained several beng-lb instances?  do we need to
		   have a configuration setting for this? */
		if (const auto *generator_header = _headers.Remove(x_cm4all_generator_header))
			rl.generator = generator_header;

	HttpHeaders headers(std::move(_headers));
	headers.generate_date_header = false;
	headers.generate_server_header = false;

	if (request.method == HttpMethod::HEAD && !connection.IsHTTP2())
		/* pass Content-Length, even though there is no response body
		   (RFC 2616 14.13) */
		headers.MoveToBuffer(content_length_header);

	if (new_cookie != 0) {
		headers.Write("cookie2", "$Version=\"1\"");

		/* "Discard" must be last, to work around an Android bug*/
		headers.Fmt("set-cookie",
			    "beng_lb_node=0-{:x}; HttpOnly; Path=/; Version=1; Discard", new_cookie);
	}

	auto &_request = request;
	Destroy();

	_request.SendResponse(status, std::move(headers),
			      std::move(response_body));
}

void
LbRequest::OnHttpError(std::exception_ptr ep) noexcept
{
	if (IsHttpClientServerFailure(ep))
		failure->SetProtocol(GetEventLoop().SteadyNow(),
				     std::chrono::seconds(20));

	connection.logger(2, ep);

	auto &_connection = connection;
	auto &_request = request;
	const auto &fallback = cluster_config.fallback;

	Destroy();

	if (!send_fallback(_request, fallback))
		_connection.SendError(_request, ep);
}

void
LbRequest::OnFilteredSocketReady(Lease &lease,
				 FilteredSocket &socket,
				 SocketAddress, const char *name,
				 ReferencedFailureInfo &_failure) noexcept
{
	failure = _failure;

	SetForwardedTo();

	auto &headers = request.headers;
	lb_forward_request_headers(pool, headers,
				   request.local_host_and_port,
				   request.remote_host,
				   connection.IsEncrypted(),
				   connection.GetPeerSubject(),
				   connection.GetPeerIssuerSubject(),
				   cluster_config.mangle_via);

	if (!cluster_config.http_host.empty())
		headers.SecureSet(pool, host_header,
				  cluster_config.http_host.c_str());

	http_client_request(pool, nullptr,
			    socket, lease, name,
			    request.method, request.uri,
			    headers, {},
			    std::move(body), true,
			    *this, cancel_ptr);
}

void
LbRequest::OnFilteredSocketError(std::exception_ptr ep) noexcept
{
	connection.logger(2, "Connect error: ", ep);

	body.Clear();

	auto &_connection = connection;
	auto &_request = request;
	const auto &fallback = cluster_config.fallback;

	Destroy();

	if (!send_fallback(_request, fallback))
		_connection.SendError(_request, ep);
}

/*
 * constructor
 *
 */

inline uint_fast64_t
LbRequest::MakeFairnessHash() const noexcept
{
	if (!cluster_config.fair_scheduling)
		return 0;

	const char *host = GetCanonicalHost();
	if (host == nullptr)
		return FNV1aHash64(std::span<const std::byte>{});

	return FNV1aHash64(host);
}

inline SocketAddress
LbRequest::MakeBindAddress() const noexcept
{
	if (cluster_config.transparent_source) {
		SocketAddress bind_address = request.remote_address;

		/* reset the port to 0 to allow the kernel to choose one */
		if (bind_address.GetFamily() == AF_INET) {
			auto &address = *NewFromPool<IPv4Address>(pool,
								  IPv4Address(bind_address));
			address.SetPort(0);
			return address;
		} else if (bind_address.GetFamily() == AF_INET6) {
			auto &address = *NewFromPool<IPv6Address>(pool,
								  IPv6Address(bind_address));
			address.SetPort(0);
			return address;
		}

		return bind_address;
	} else
		return SocketAddress::Null();
}

inline void
LbRequest::Start() noexcept
{
	const auto &rl = *(const LbRequestLogger *)request.logger;

	cluster.ConnectHttp(pool, nullptr,
			    MakeFairnessHash(),
			    MakeBindAddress(),
			    rl.arch,
			    GetStickySource(),
			    GetStickyHash(),
			    LB_HTTP_CONNECT_TIMEOUT,
			    *this, cancel_ptr);
}

void
ForwardHttpRequest(LbHttpConnection &connection,
		   IncomingHttpRequest &request,
		   LbCluster &cluster,
		   CancellablePointer &cancel_ptr) noexcept
{
	const auto request2 =
		NewFromPool<LbRequest>(request.pool,
				       connection, cluster,
				       request, cancel_ptr);
	request2->Start();
}
