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

#include "Glue.hxx"
#include "Stock.hxx"
#include "Address.hxx"
#include "http/Client.hxx"
#include "http/PendingRequest.hxx"
#include "http/ResponseHandler.hxx"
#include "memory/GrowingBuffer.hxx"
#include "fs/FilteredSocket.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "lease.hxx"
#include "istream/UnusedPtr.hxx"
#include "http/HeaderWriter.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"

class LhttpLease final : public Lease, PoolLeakDetector {
	StockItem &stock_item;

	FilteredSocket socket;

public:
	explicit LhttpLease(struct pool &_pool, EventLoop &event_loop,
			    StockItem &_stock_item) noexcept
		:PoolLeakDetector(_pool),
		 stock_item(_stock_item), socket(event_loop)
	{
		socket.InitDummy(lhttp_stock_item_get_socket(stock_item),
				 lhttp_stock_item_get_type(stock_item));
	}

	auto &GetSocket() noexcept {
		return socket;
	}

private:
	void Destroy() {
		this->~LhttpLease();
	}

	/* virtual methods from class Lease */
	void ReleaseLease(bool reuse) noexcept override {
		if (socket.IsConnected())
			socket.Abandon();

		stock_item.Put(!reuse);

		Destroy();
	}
};

class LhttpRequest final
	: Cancellable, StockGetHandler, HttpResponseHandler, PoolLeakDetector
{
	struct pool &pool;
	EventLoop &event_loop;
	LhttpStock &stock;

	StopwatchPtr stopwatch;

	const char *const site_name;

	unsigned retries;

	const LhttpAddress &address;

	PendingHttpRequest pending_request;

	HttpResponseHandler &handler;
	CancellablePointer cancel_ptr;

public:
	explicit LhttpRequest(struct pool &_pool,
			      EventLoop &_event_loop,
			      LhttpStock &_stock,
			      StopwatchPtr &&_stopwatch,
			      const char *_site_name,
			      http_method_t _method,
			      const LhttpAddress &_address,
			      StringMap &&_headers,
			      UnusedIstreamPtr &&_body,
			      HttpResponseHandler &_handler,
			      CancellablePointer &_cancel_ptr) noexcept
		:PoolLeakDetector(_pool), pool(_pool), event_loop(_event_loop),
		 stock(_stock),
		 stopwatch(std::move(_stopwatch)),
		 site_name(_site_name),
		 /* can only retry if there is no request body */
		 retries(_body ? 0 : 1),
		 address(_address),
		 pending_request(_pool, _method, _address.uri,
				 std::move(_headers), std::move(_body)),
		 handler(_handler)
	{
		_cancel_ptr = *this;
	}

	void Start() noexcept;

private:
	void Destroy() {
		this->~LhttpRequest();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr error) noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(http_status_t status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

inline void
LhttpRequest::Start() noexcept
{
	lhttp_stock_get(&stock, &address,
			*this, cancel_ptr);
}

void
LhttpRequest::OnStockItemReady(StockItem &item) noexcept
{
	stopwatch.RecordEvent("launch");

	lhttp_stock_item_set_site(item, site_name);
	lhttp_stock_item_set_uri(item, address.uri);

	GrowingBuffer more_headers;
	if (address.host_and_port != nullptr)
		header_write(more_headers, "host",
			     address.host_and_port);

	auto *lease = NewFromPool<LhttpLease>(pool, pool, event_loop,
					      item);

	http_client_request(pool, std::move(stopwatch),
			    lease->GetSocket(), *lease,
			    item.GetStockName(),
			    pending_request.method, pending_request.uri,
			    pending_request.headers, std::move(more_headers),
			    std::move(pending_request.body), true,
			    *this, cancel_ptr);
}

void
LhttpRequest::OnStockItemError(std::exception_ptr error) noexcept
{
	stopwatch.RecordEvent("launch_error");

	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(std::move(error));
}

void
LhttpRequest::OnHttpResponse(http_status_t status, StringMap &&_headers,
			    UnusedIstreamPtr _body) noexcept
{
	auto &_handler = handler;
	Destroy();
	_handler.InvokeResponse(status, std::move(_headers), std::move(_body));
}

void
LhttpRequest::OnHttpError(std::exception_ptr ep) noexcept
{
	if (retries > 0 && IsHttpClientRetryFailure(ep)) {
		/* the server has closed the connection prematurely, maybe
		   because it didn't want to get any further requests on that
		   TCP connection.  Let's try again. */

		--retries;

		/* passing no request body; retry is only ever enabled
		   if there is no request body */
		Start();
	} else {
		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(ep);
	}
}

/*
 * constructor
 *
 */

void
lhttp_request(struct pool &pool, EventLoop &event_loop,
	      LhttpStock &lhttp_stock,
	      const StopwatchPtr &parent_stopwatch,
	      const char *site_name,
	      const LhttpAddress &address,
	      http_method_t method, StringMap &&headers,
	      UnusedIstreamPtr body,
	      HttpResponseHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept
{
	StopwatchPtr stopwatch(parent_stopwatch, address.uri);

	try {
		address.options.Check();
	} catch (...) {
		stopwatch.RecordEvent("error");

		body.Clear();

		handler.InvokeError(std::current_exception());
		return;
	}

	auto request = NewFromPool<LhttpRequest>(pool, pool, event_loop,
						 lhttp_stock,
						 std::move(stopwatch),
						 site_name, method, address,
						 std::move(headers),
						 std::move(body),
						 handler, cancel_ptr);

	request->Start();
}
