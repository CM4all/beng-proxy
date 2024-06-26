// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "Stock.hxx"
#include "Client.hxx"
#include "http/PendingRequest.hxx"
#include "http/ResponseHandler.hxx"
#include "cgi/Address.hxx"
#include "stock/AbstractStock.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"
#include "lease.hxx"
#include "stopwatch.hxx"

#include <sys/socket.h>

class FcgiRequest final
	: Lease, Cancellable, StockGetHandler, PoolLeakDetector
{
	struct pool &pool;

	StopwatchPtr stopwatch;

	const CgiAddress &address;

	PendingHttpRequest pending_request;

	const char *const site_name;
	const char *const remote_addr;

	UniqueFileDescriptor stderr_fd;

	StockItem *stock_item = nullptr;

	HttpResponseHandler &handler;
	CancellablePointer cancel_ptr;

public:
	FcgiRequest(struct pool &_pool,
		    const StopwatchPtr &parent_stopwatch,
		    const char *_site_name,
		    const CgiAddress &_address,
		    const char *action,
		    HttpMethod _method,
		    const char *_remote_addr,
		    StringMap &&_headers, UnusedIstreamPtr &&_body,
		    UniqueFileDescriptor &&_stderr_fd,
		    HttpResponseHandler &_handler) noexcept
		:PoolLeakDetector(_pool),
		 pool(_pool),
		 stopwatch(parent_stopwatch, "fcgi", action),
		 address(_address),
		 pending_request(_pool, _method, address.GetURI(pool),
				 std::move(_headers), std::move(_body)),
		 site_name(_site_name),
		 remote_addr(_remote_addr),
		 stderr_fd(std::move(_stderr_fd)),
		 handler(_handler)
	{
	}

	void Start(FcgiStock &fcgi_stock, const char *action,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;

		fcgi_stock_get(&fcgi_stock, address.options,
			       action, address.args.ToArray(pool),
			       address.parallelism,
			       *this, cancel_ptr);
	}

private:
	void Destroy() noexcept {
		DeleteFromPool(pool, this);
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		if (stock_item == nullptr) {
			cancel_ptr.Cancel();
			Destroy();
		} else {
			fcgi_stock_aborted(*stock_item);
			cancel_ptr.Cancel();
		}
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr error) noexcept override;

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override {
		auto &_item = *stock_item;
		Destroy();
		return _item.Put(action);
	}
};

void
FcgiRequest::OnStockItemReady(StockItem &item) noexcept
{
	assert(stock_item == nullptr);
	stock_item = &item;

	stopwatch.RecordEvent("launch");

	fcgi_stock_item_set_site(*stock_item, site_name);
	fcgi_stock_item_set_uri(*stock_item, pending_request.uri);

	if (!stderr_fd.IsDefined())
		stderr_fd = fcgi_stock_item_get_stderr(*stock_item);

	const char *script_filename = address.path;

	fcgi_client_request(&pool, item.GetStock().GetEventLoop(), std::move(stopwatch),
			    fcgi_stock_item_get(*stock_item),
			    fcgi_stock_item_get_domain(*stock_item) == AF_LOCAL
			    ? FdType::FD_SOCKET : FdType::FD_TCP,
			    *this,
			    pending_request.method, pending_request.uri,
			    script_filename,
			    address.script_name, address.path_info,
			    address.query_string,
			    address.document_root,
			    remote_addr,
			    std::move(pending_request.headers),
			    std::move(pending_request.body),
			    address.params.ToArray(pool),
			    std::move(stderr_fd),
			    handler, cancel_ptr);
}

void
FcgiRequest::OnStockItemError(std::exception_ptr error) noexcept
{
	assert(stock_item == nullptr);

	stopwatch.RecordEvent("launch_error");

	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(std::move(error));
}

void
fcgi_request(struct pool *pool,
	     FcgiStock *fcgi_stock,
	     const StopwatchPtr &parent_stopwatch,
	     const char *site_name,
	     const CgiAddress &address,
	     HttpMethod method,
	     const char *remote_addr,
	     StringMap &&headers, UnusedIstreamPtr body,
	     UniqueFileDescriptor &&stderr_fd,
	     HttpResponseHandler &handler,
	     CancellablePointer &cancel_ptr) noexcept
{
	const char *action = address.action;
	if (action == nullptr)
		action = address.path;

	auto *request = NewFromPool<FcgiRequest>(*pool, *pool, parent_stopwatch,
						 site_name, address, action, method,
						 remote_addr,
						 std::move(headers), std::move(body),
						 std::move(stderr_fd),
						 handler);
	request->Start(*fcgi_stock, action, cancel_ptr);
}
