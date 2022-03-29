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

#include "HttpConnection.hxx"
#include "RLogger.hxx"
#include "Headers.hxx"
#include "Instance.hxx"
#include "pool/PSocketAddress.hxx"
#include "lease.hxx"
#include "http/ResponseHandler.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Client.hxx"
#include "http/Headers.hxx"
#include "ssl/Filter.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "fs/Stock.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "net/Resolver.hxx"
#include "net/AddressInfo.hxx"
#include "util/Cancellable.hxx"
#include "util/LeakDetector.hxx"
#include "stopwatch.hxx"

static constexpr Event::Duration LB_HTTP_CONNECT_TIMEOUT =
	std::chrono::seconds(20);

class LbResolveConnectRequest final
	: LeakDetector, Cancellable, StockGetHandler, Lease, HttpResponseHandler {

	struct pool &pool;

	LbHttpConnection &connection;

	IncomingHttpRequest &request;

	/**
	 * The request body.
	 */
	UnusedHoldIstreamPtr body;

	CancellablePointer cancel_ptr;

	StockItem *stock_item;

	bool response_sent = false, reuse;

	enum class LeaseState : uint8_t {
		NONE,
		BUSY,
		PENDING,
	} lease_state = LeaseState::NONE;

public:
	LbResolveConnectRequest(LbHttpConnection &_connection,
				IncomingHttpRequest &_request,
				CancellablePointer &_cancel_ptr)
		:pool(_request.pool), connection(_connection),
		 request(_request),
		 body(pool, std::move(request.body)) {
		_cancel_ptr = *this;
	}

	void Start(const char *name, SocketAddress address);

private:
	void Destroy() noexcept {
		assert(lease_state == LeaseState::NONE);

		DeleteFromPool(pool, this);
	}

	void DoRelease() {
		assert(lease_state == LeaseState::PENDING);

		lease_state = LeaseState::NONE;
		stock_item->Put(!reuse);
	}

	bool CheckRelease() {
		if (lease_state == LeaseState::PENDING)
			DoRelease();
		return lease_state == LeaseState::NONE;
	}

	void ResponseSent() {
		assert(!response_sent);
		response_sent = true;

		if (CheckRelease())
			Destroy();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		assert(!response_sent);

		CheckRelease();
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

void
LbResolveConnectRequest::OnStockItemReady(StockItem &item) noexcept
{
	assert(lease_state == LeaseState::NONE);
	assert(!response_sent);

	stock_item = &item;
	lease_state = LeaseState::BUSY;

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
				   false);

	http_client_request(pool, nullptr,
			    fs_stock_item_get(item),
			    *this,
			    item.GetStockName(),
			    request.method, request.uri,
			    headers, {},
			    std::move(body), true,
			    *this, cancel_ptr);
}

void
LbResolveConnectRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	assert(lease_state == LeaseState::NONE);
	assert(!response_sent);

	connection.logger(2, "Connect error: ", ep);

	body.Clear();

	auto &_connection = connection;
	auto &_request = request;
	Destroy();
	_connection.SendError(_request, std::move(ep));
}

void
LbResolveConnectRequest::ReleaseLease(bool _reuse) noexcept
{
	assert(lease_state == LeaseState::BUSY);

	lease_state = LeaseState::PENDING;
	reuse = _reuse;

	if (response_sent) {
		DoRelease();
		Destroy();
	}
}

void
LbResolveConnectRequest::OnHttpResponse(http_status_t status, StringMap &&_headers,
					UnusedIstreamPtr response_body) noexcept
{
	assert(lease_state != LeaseState::NONE);
	assert(!response_sent);

	HttpHeaders headers(std::move(_headers));

	if (request.method == HTTP_METHOD_HEAD && !connection.IsHTTP2())
		/* pass Content-Length, even though there is no response body
		   (RFC 2616 14.13) */
		headers.MoveToBuffer("content-length");

	if (CheckRelease()) {
		/* the connection lease was already released by the
		   HTTP client: destroy this object before invoking
		   the response to avoid use-after-free when
		   SendResponse() frees the memory pool */
		auto &_request = request;
		Destroy();
		_request.SendResponse(status, std::move(headers),
				      std::move(response_body));;
	} else {
		request.SendResponse(status, std::move(headers),
				     std::move(response_body));
		ResponseSent();
	}
}

void
LbResolveConnectRequest::OnHttpError(std::exception_ptr ep) noexcept
{
	assert(lease_state == LeaseState::PENDING);
	assert(!response_sent);

	connection.logger(2, ep);

	DoRelease();

	auto &_connection = connection;
	auto &_request = request;
	Destroy();
	_connection.SendError(_request, std::move(ep));
}

inline void
LbResolveConnectRequest::Start(const char *name, SocketAddress address)
{
	connection.instance.fs_stock->Get(pool, nullptr,
					  name, 0, false, nullptr,
					  address,
					  LB_HTTP_CONNECT_TIMEOUT,
					  nullptr,
					  *this, cancel_ptr);

}

void
LbHttpConnection::ResolveConnect(const char *host,
				 IncomingHttpRequest &request,
				 CancellablePointer &cancel_ptr)
{
	auto &rl = *(LbRequestLogger *)request.logger;
	rl.forwarded_to = host;

	SocketAddress address;

	try {
		static constexpr auto hints = MakeAddrInfo(AI_ADDRCONFIG,
							   AF_UNSPEC,
							   SOCK_STREAM);

		/* TODO: make this lookup non-blocking */
		address = DupAddress(*request.pool,
				     Resolve(host, 80, &hints).front());
	} catch (...) {
		SendError(request, std::current_exception());
		return;
	}

	const auto request2 =
		NewFromPool<LbResolveConnectRequest>(request.pool, *this,
						     request, cancel_ptr);
	request2->Start(host, address);
}
