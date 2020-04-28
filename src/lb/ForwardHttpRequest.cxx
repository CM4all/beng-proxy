/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "ForwardHttpRequest.hxx"
#include "RLogger.hxx"
#include "HttpConnection.hxx"
#include "Cluster.hxx"
#include "ClusterConfig.hxx"
#include "ListenerConfig.hxx"
#include "Instance.hxx"
#include "Session.hxx"
#include "Cookie.hxx"
#include "JvmRoute.hxx"
#include "Headers.hxx"
#include "ssl/Filter.hxx"
#include "cluster/AddressSticky.hxx"
#include "address_string.hxx"
#include "http_server/http_server.hxx"
#include "http/IncomingRequest.hxx"
#include "http_server/Handler.hxx"
#include "http_client.hxx"
#include "fs/Handler.hxx"
#include "HttpResponseHandler.hxx"
#include "http/Headers.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "stock/Lease.hxx"
#include "strmap.hxx"
#include "pool/pool.hxx"
#include "net/IPv4Address.hxx"
#include "net/IPv6Address.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/FailureManager.hxx"
#include "net/FailureRef.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "util/Cancellable.hxx"
#include "util/LeakDetector.hxx"
#include "util/FNVHash.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

static constexpr Event::Duration LB_HTTP_CONNECT_TIMEOUT =
	std::chrono::seconds(20);

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
		// TODO: optimize this operation
		auto &rl = *(LbRequestLogger *)request.logger;
		rl.forwarded_to =
			address_to_string(pool,
					  GetFailureManager().GetAddress(*failure));
	}

	const char *GetCanonicalHost() const noexcept {
		auto &rl = *(LbRequestLogger *)request.logger;
		return rl.GetCanonicalHost();
	}

	sticky_hash_t GetStickyHash() noexcept;
	sticky_hash_t GetHostHash() const noexcept;
	sticky_hash_t GetXHostHash() const noexcept;
	sticky_hash_t MakeCookieHash() noexcept;

	SocketAddress MakeBindAddress() const noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
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
	void OnHttpResponse(http_status_t status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

static void
SendResponse(IncomingHttpRequest &request,
	     const LbSimpleHttpResponse &response) noexcept
{
	assert(response.IsDefined());

	request.SendSimpleResponse(response.status,
				   response.location.empty() ? nullptr : response.location.c_str(),
				   response.message.empty() ? nullptr : response.message.c_str());
}

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
	const char *host = request.headers.Get("x-cm4all-host");
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
	assert(list.GetSize() >= 2);

	const unsigned first = lb_cookie_generate(list.GetSize());

	unsigned i = first;
	do {
		assert(i >= 1 && i <= list.GetSize());
		const SocketAddress address = list.addresses[i % list.GetSize()];
		if (failure_manager.Check(now, address))
			return i;

		i = lb_cookie_next(list.GetSize(), i);
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
		return 0;

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

	assert(false);
	gcc_unreachable();
}

/*
 * HTTP response handler
 *
 */

void
LbRequest::OnHttpResponse(http_status_t status, StringMap &&_headers,
			  UnusedIstreamPtr response_body) noexcept
{
	failure->UnsetProtocol();

	SetForwardedTo();

	HttpHeaders headers(std::move(_headers));

	if (request.method == HTTP_METHOD_HEAD)
		/* pass Content-Length, even though there is no response body
		   (RFC 2616 14.13) */
		headers.MoveToBuffer("content-length");

	if (new_cookie != 0) {
		char buffer[64];
		/* "Discard" must be last, to work around an Android bug*/
		snprintf(buffer, sizeof(buffer),
			 "beng_lb_node=0-%x; HttpOnly; Path=/; Version=1; Discard",
			 new_cookie);

		headers.Write("cookie2", "$Version=\"1\"");
		headers.Write("set-cookie", buffer);
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

	SetForwardedTo();

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

	const char *peer_subject = connection.ssl_filter != nullptr
		? ssl_filter_get_peer_subject(*connection.ssl_filter)
		: nullptr;
	const char *peer_issuer_subject = connection.ssl_filter != nullptr
		? ssl_filter_get_peer_issuer_subject(*connection.ssl_filter)
		: nullptr;

	auto &headers = request.headers;
	lb_forward_request_headers(pool, headers,
				   request.local_host_and_port,
				   request.remote_host,
				   connection.IsEncrypted(),
				   peer_subject, peer_issuer_subject,
				   cluster_config.mangle_via);

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
	cluster.ConnectHttp(pool, nullptr,
			    MakeBindAddress(),
			    GetStickyHash(),
			    LB_HTTP_CONNECT_TIMEOUT,
			    nullptr,
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
