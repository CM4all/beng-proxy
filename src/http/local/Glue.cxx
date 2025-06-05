// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Glue.hxx"
#include "Stock.hxx"
#include "Connection.hxx"
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
#include "event/FineTimerEvent.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"

class LhttpLease final : public Lease, PoolLeakDetector {
	StockItem &stock_item;

public:
	explicit LhttpLease(struct pool &_pool,
			    StockItem &_stock_item) noexcept
		:PoolLeakDetector(_pool),
		 stock_item(_stock_item)
	{
	}

	auto &GetSocket() noexcept {
		return lhttp_stock_item_get_socket(stock_item);
	}

private:
	void Destroy() noexcept {
		this->~LhttpLease();
	}

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override {
		auto &_item = stock_item;
		Destroy();
		return _item.Put(action);
	}
};

class LhttpRequest final
	: Cancellable, StockGetHandler, HttpResponseHandler, PoolLeakDetector
{
	struct pool &pool;
	LhttpStock &stock;

	/**
	 * This timer delays retry attempts a bit to avoid the load
	 * getting too heavy for retries and to handle child process
	 * exit messages in the meantime; the latter avoids opening a
	 * new connection to a dying child process.
	 */
	FineTimerEvent retry_timer;

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
			      HttpMethod _method,
			      const LhttpAddress &_address,
			      StringMap &&_headers,
			      UnusedIstreamPtr &&_body,
			      HttpResponseHandler &_handler,
			      CancellablePointer &_cancel_ptr) noexcept
		:PoolLeakDetector(_pool), pool(_pool),
		 stock(_stock),
		 retry_timer(_event_loop, BIND_THIS_METHOD(Start)),
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

	auto &GetEventLoop() const noexcept {
		return retry_timer.GetEventLoop();
	}

	void Start() noexcept;

private:
	void Destroy() {
		this->~LhttpRequest();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		if (cancel_ptr)
			cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr error) noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

inline void
LhttpRequest::Start() noexcept
{
	stock.Get(address, *this, cancel_ptr);
}

void
LhttpRequest::OnStockItemReady(StockItem &item) noexcept
{
	cancel_ptr = {};

	stopwatch.RecordEvent("launch");

	lhttp_stock_item_set_site(item, site_name);
	lhttp_stock_item_set_uri(item, address.uri);

	GrowingBuffer more_headers;
	if (address.host_and_port != nullptr)
		header_write(more_headers, "host",
			     address.host_and_port);

	auto *lease = NewFromPool<LhttpLease>(pool, pool, item);

	http_client_request(pool, std::move(stopwatch),
			    lease->GetSocket(), *lease,
			    item.GetStockNameC(),
			    pending_request.method, pending_request.uri,
			    pending_request.headers, std::move(more_headers),
			    std::move(pending_request.body), true,
			    *this, cancel_ptr);
}

void
LhttpRequest::OnStockItemError(std::exception_ptr error) noexcept
{
	cancel_ptr = {};

	if (retries > 0 && IsHttpClientRetryFailure(error)) {
		/* the server has closed the connection prematurely,
		   maybe because it didn't want to get any further
		   requests on that connection.  Let's try again. */

		--retries;
		retry_timer.Schedule(std::chrono::milliseconds{100});
		return;
	}

	stopwatch.RecordEvent("launch_error");

	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(std::move(error));
}

void
LhttpRequest::OnHttpResponse(HttpStatus status, StringMap &&_headers,
			    UnusedIstreamPtr _body) noexcept
{
	cancel_ptr = {};

	auto &_handler = handler;
	Destroy();
	_handler.InvokeResponse(status, std::move(_headers), std::move(_body));
}

void
LhttpRequest::OnHttpError(std::exception_ptr ep) noexcept
{
	cancel_ptr = {};

	if (retries > 0 && IsHttpClientRetryFailure(ep)) {
		/* the server has closed the connection prematurely, maybe
		   because it didn't want to get any further requests on that
		   TCP connection.  Let's try again. */

		--retries;

		/* passing no request body; retry is only ever enabled
		   if there is no request body */
		retry_timer.Schedule(std::chrono::milliseconds{20});
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
	      HttpMethod method, StringMap &&headers,
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
