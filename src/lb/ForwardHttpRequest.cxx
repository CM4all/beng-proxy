/*
 * Copyright 2007-2019 CM4all GmbH
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
#include "address_sticky.hxx"
#include "address_string.hxx"
#include "http_server/http_server.hxx"
#include "http/IncomingRequest.hxx"
#include "http_server/Handler.hxx"
#include "http_client.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "HttpResponseHandler.hxx"
#include "http/Headers.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "lease.hxx"
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
	: LeakDetector, Cancellable, StockGetHandler, Lease, HttpResponseHandler {

	struct pool &pool;

	LbHttpConnection &connection;
	LbCluster &cluster;
	const LbClusterConfig &cluster_config;

	FilteredSocketBalancer &balancer;

	IncomingHttpRequest &request;

	/**
	 * The request body.
	 */
	UnusedHoldIstreamPtr body;

	CancellablePointer cancel_ptr;

	/**
	 * The cluster member we're currently connecting to (waiting for
	 * #StockGetHandler to be called).  Only used for Zeroconf to be
	 * able to call failure_set().
	 */
	LbCluster::MemberPtr current_member;

	StockItem *stock_item = nullptr;
	FailurePtr failure;

	/**
	 * The number of remaining connection attempts.  We give up when
	 * we get an error and this attribute is already zero.
	 */
	unsigned retries;

	unsigned new_cookie = 0;

	bool response_sent = false;

public:
	LbRequest(LbHttpConnection &_connection, LbCluster &_cluster,
		  FilteredSocketBalancer &_balancer,
		  IncomingHttpRequest &_request,
		  CancellablePointer &_cancel_ptr) noexcept
		:pool(_request.pool), connection(_connection), cluster(_cluster),
		 cluster_config(cluster.GetConfig()),
		 balancer(_balancer),
		 request(_request),
		 body(pool, std::move(request.body)) {
		_cancel_ptr = *this;

		if (cluster_config.HasZeroConf())
			retries = CalculateRetries(cluster.GetZeroconfCount());
	}

	EventLoop &GetEventLoop() const noexcept {
		return connection.instance.event_loop;
	}

	FailureManager &GetFailureManager() noexcept {
		return balancer.GetFailureManager();
	}

	void Start() noexcept;

private:
	/* code copied from generic_balancer.hxx */
	static constexpr unsigned CalculateRetries(size_t size) noexcept {
		if (size <= 1)
			return 0;
		else if (size == 2)
			return 1;
		else if (size == 3)
			return 2;
		else
			return 3;
	}

	void Destroy() noexcept {
		assert(stock_item == nullptr);

		DeleteFromPool(pool, this);
	}

	void SetForwardedTo() noexcept {
		// TODO: optimize this operation
		connection.per_request.forwarded_to =
			address_to_string(pool,
					  GetFailureManager().GetAddress(*failure));
	}

	void ResponseSent() noexcept {
		assert(!response_sent);
		response_sent = true;

		if (stock_item == nullptr)
			Destroy();
	}

	const char *GetCanonicalHost() const noexcept {
		return connection.per_request.GetCanonicalHost();
	}

	sticky_hash_t GetStickyHash() noexcept;
	sticky_hash_t GetHostHash() const noexcept;
	sticky_hash_t GetXHostHash() const noexcept;
	sticky_hash_t MakeCookieHash() noexcept;

	SocketAddress MakeBindAddress() const noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		assert(!response_sent);

		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Lease */
	void ReleaseLease(bool reuse) noexcept override;

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
		/* calculate session_sticky from remote address */
		return socket_address_sticky(request.remote_address);

	case StickyMode::HOST:
		/* calculate session_sticky from "Host" request header */
		return GetHostHash();

	case StickyMode::XHOST:
		/* calculate session_sticky from "X-CM4all-Host" request
		   header */
		return GetXHostHash();

	case StickyMode::SESSION_MODULO:
		/* calculate session_sticky from beng-proxy session id */
		return lb_session_get(request.headers,
				      cluster_config.session_cookie.c_str());

	case StickyMode::COOKIE:
		/* calculate session_sticky from beng-lb cookie */
		return MakeCookieHash();

	case StickyMode::JVM_ROUTE:
		/* calculate session_sticky from JSESSIONID cookie suffix */
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
	assert(!response_sent);

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
	ResponseSent();

	_request.SendResponse(status, std::move(headers),
			      std::move(response_body));
}

void
LbRequest::OnHttpError(std::exception_ptr ep) noexcept
{
	assert(!response_sent);

	if (IsHttpClientServerFailure(ep))
		failure->SetProtocol(GetEventLoop().SteadyNow(),
				     std::chrono::seconds(20));

	SetForwardedTo();

	connection.logger(2, ep);

	auto &_connection = connection;
	auto &_request = request;
	const auto &fallback = cluster_config.fallback;

	ResponseSent();

	if (!send_fallback(_request, fallback))
		_connection.SendError(_request, ep);
}

/*
 * stock callback
 *
 */

void
LbRequest::OnStockItemReady(StockItem &item) noexcept
{
	assert(stock_item == nullptr);
	assert(!response_sent);

	stock_item = &item;

	if (cluster_config.HasZeroConf()) {
		failure = current_member->GetFailureRef();

		/* without the fs_balancer, we have to roll our own failure
		   updates */
		failure->UnsetConnect();
	} else
		failure = GetFailureManager().Make(fs_stock_item_get_address(*stock_item));

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
			    fs_stock_item_get(item),
			    *this,
			    item.GetStockName(),
			    request.method, request.uri,
			    HttpHeaders(std::move(headers)),
			    std::move(body), true,
			    *this, cancel_ptr);
}

void
LbRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	assert(stock_item == nullptr);
	assert(!response_sent);

	connection.logger(2, "Connect error: ", ep);

	if (cluster_config.HasZeroConf()) {
		/* without the tcp_balancer, we have to roll our own failure
		   updates and retries */
		current_member->GetFailureInfo().SetConnect(GetEventLoop().SteadyNow(),
							    std::chrono::seconds(20));

		if (retries-- > 0) {
			/* try the next Zeroconf member */
			Start();
			return;
		}
	}

	body.Clear();

	auto &_connection = connection;
	auto &_request = request;
	const auto &fallback = cluster_config.fallback;

	ResponseSent();

	if (!send_fallback(_request, fallback))
		_connection.SendError(_request, ep);
}

/*
 * Lease
 *
 */

void
LbRequest::ReleaseLease(bool reuse) noexcept
{
	assert(stock_item != nullptr);

	stock_item->Put(!reuse);
	stock_item = nullptr;

	if (response_sent) {
		Destroy();
	}
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
	const auto bind_address = MakeBindAddress();

	if (cluster_config.HasZeroConf()) {
		auto *member = cluster.Pick(GetEventLoop().SteadyNow(),
					    GetStickyHash());
		if (member == nullptr) {
			auto &_request = request;
			Destroy();
			_request.SendMessage(HTTP_STATUS_INTERNAL_SERVER_ERROR,
					     "Zeroconf cluster is empty");
			return;
		}

		current_member = *member;

		connection.instance.fs_stock->Get(pool,
						  nullptr,
						  member->GetLogName(),
						  cluster_config.transparent_source,
						  bind_address,
						  member->GetAddress(),
						  LB_HTTP_CONNECT_TIMEOUT,
						  nullptr,
						  *this, cancel_ptr);

		return;
	}

	balancer.Get(pool, nullptr,
		     cluster_config.transparent_source,
		     bind_address,
		     GetStickyHash(),
		     cluster_config.address_list,
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
				       *connection.instance.fs_balancer,
				       request, cancel_ptr);
	request2->Start();
}
