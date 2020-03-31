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

#include "http_request.hxx"
#include "http_client.hxx"
#include "http/Headers.hxx"
#include "http/Address.hxx"
#include "HttpResponseHandler.hxx"
#include "http/HeaderWriter.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "stock/Lease.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "fs/Factory.hxx"
#include "fs/SocketFilter.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "event/Loop.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/FailureManager.hxx"
#include "net/FailureRef.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "util/Compiler.h"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

#include <string.h>

static constexpr Event::Duration HTTP_CONNECT_TIMEOUT =
	std::chrono::seconds(30);

class HttpRequest final
	: Cancellable, StockGetHandler, HttpResponseHandler, PoolLeakDetector {

	struct pool &pool;

	EventLoop &event_loop;

	FilteredSocketBalancer &fs_balancer;

	StopwatchPtr stopwatch;

	SocketFilterFactory *const filter_factory;

	FailurePtr failure;

	const sticky_hash_t session_sticky;

	unsigned retries;

	const http_method_t method;
	const HttpAddress &address;
	HttpHeaders headers;
	UnusedHoldIstreamPtr body;

	HttpResponseHandler &handler;
	CancellablePointer cancel_ptr;

public:
	HttpRequest(struct pool &_pool, EventLoop &_event_loop,
		    FilteredSocketBalancer &_fs_balancer,
		    const StopwatchPtr &parent_stopwatch,
		    sticky_hash_t _session_sticky,
		    SocketFilterFactory *_filter_factory,
		    http_method_t _method,
		    const HttpAddress &_address,
		    HttpHeaders &&_headers,
		    UnusedIstreamPtr _body,
		    HttpResponseHandler &_handler,
		    CancellablePointer &_cancel_ptr)
		:PoolLeakDetector(_pool),
		 pool(_pool), event_loop(_event_loop), fs_balancer(_fs_balancer),
		 stopwatch(parent_stopwatch, _address.path),
		 filter_factory(_filter_factory),
		 session_sticky(_session_sticky),
		 /* can only retry if there is no request body */
		 retries(_body ? 0 : 2),
		 method(_method), address(_address),
		 headers(std::move(_headers)), body(pool, std::move(_body)),
		 handler(_handler)
	{
		_cancel_ptr = *this;

		if (address.host_and_port != nullptr)
			headers.Write("host", address.host_and_port);
	}

	void BeginConnect() {
		fs_balancer.Get(pool, stopwatch,
				false, SocketAddress::Null(),
				session_sticky,
				address.addresses,
				HTTP_CONNECT_TIMEOUT,
				filter_factory,
				*this, cancel_ptr);
	}

private:
	void Destroy() noexcept {
		DeleteFromPool(pool, this);
	}

	void Failed(std::exception_ptr ep) {
		body.Clear();
		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(ep);
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(http_status_t status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

/*
 * HTTP response handler
 *
 */

void
HttpRequest::OnHttpResponse(http_status_t status, StringMap &&_headers,
			    UnusedIstreamPtr _body) noexcept
{
	failure->UnsetProtocol();

	auto &_handler = handler;
	Destroy();
	_handler.InvokeResponse(status, std::move(_headers), std::move(_body));
}

static bool
HasHttpClientErrorCode(std::exception_ptr ep,
		       HttpClientErrorCode code) noexcept
{
	try {
		FindRetrowNested<HttpClientError>(ep);
		return false;
	} catch (const HttpClientError &e) {
		return e.GetCode() == code;
	}
}

void
HttpRequest::OnHttpError(std::exception_ptr ep) noexcept
{
	if (retries > 0 &&
	    HasHttpClientErrorCode(ep, HttpClientErrorCode::REFUSED)) {
		/* the server has closed the connection prematurely, maybe
		   because it didn't want to get any further requests on that
		   TCP connection.  Let's try again. */

		--retries;
		BeginConnect();
	} else {
		if (IsHttpClientServerFailure(ep)) {
			failure->SetProtocol(event_loop.SteadyNow(),
					     std::chrono::seconds(20));
		}

		Failed(ep);
	}
}

/*
 * stock callback
 *
 */

void
HttpRequest::OnStockItemReady(StockItem &item) noexcept
{
	stopwatch.RecordEvent("connect");

	failure = fs_balancer.GetFailureManager()
		.Make(fs_stock_item_get_address(item));

	http_client_request(pool, std::move(stopwatch),
			    fs_stock_item_get(item),
			    *NewFromPool<StockItemLease>(pool, item),
			    item.GetStockName(),
			    method, address.path, std::move(headers),
			    std::move(body), true,
			    *this, cancel_ptr);
}

void
HttpRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("connect_error");

	Failed(ep);
}

/*
 * constructor
 *
 */

void
http_request(struct pool &pool, EventLoop &event_loop,
	     FilteredSocketBalancer &fs_balancer,
	     const StopwatchPtr &parent_stopwatch,
	     sticky_hash_t session_sticky,
	     SocketFilterFactory *filter_factory,
	     http_method_t method,
	     const HttpAddress &uwa,
	     HttpHeaders &&headers,
	     UnusedIstreamPtr body,
	     HttpResponseHandler &handler,
	     CancellablePointer &_cancel_ptr)
{
	assert(uwa.host_and_port != nullptr);
	assert(uwa.path != nullptr);

	auto hr = NewFromPool<HttpRequest>(pool, pool, event_loop, fs_balancer,
					   parent_stopwatch,
					   session_sticky,
					   filter_factory,
					   method, uwa,
					   std::move(headers), std::move(body),
					   handler, _cancel_ptr);

	hr->BeginConnect();
}
