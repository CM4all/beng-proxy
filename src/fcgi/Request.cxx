// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "Stock.hxx"
#include "SConnection.hxx"
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
#include "event/FineTimerEvent.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"
#include "lease.hxx"
#include "stopwatch.hxx"

#include <sys/socket.h>

class FcgiRequest final
	: Lease, Cancellable, StockGetHandler, HttpResponseHandler, PoolLeakDetector
{
	struct pool &pool;
	FcgiStock &fcgi_stock;

	/**
	 * This timer delays retry attempts a bit to avoid the load
         * getting too heavy for retries and to handle child process
         * exit messages in the meantime; the latter avoids opening a
         * new connection to a dying child process.
	 */
	FineTimerEvent retry_timer;

	StopwatchPtr stopwatch;

	const CgiAddress &address;

	PendingHttpRequest pending_request;

	const char *const site_name;
	const char *const action;
	const char *const remote_addr;

	UniqueFileDescriptor stderr_fd;

	StockItem *stock_item = nullptr;

	HttpResponseHandler &handler;
	CancellablePointer cancel_ptr;

	unsigned retries;

public:
	FcgiRequest(struct pool &_pool,
		    FcgiStock &_fcgi_stock,
		    const StopwatchPtr &parent_stopwatch,
		    const char *_site_name,
		    const CgiAddress &_address,
		    const char *_action,
		    HttpMethod _method,
		    const char *_remote_addr,
		    StringMap &&_headers, UnusedIstreamPtr &&_body,
		    UniqueFileDescriptor &&_stderr_fd,
		    HttpResponseHandler &_handler,
		    CancellablePointer &caller_cancel_ptr) noexcept
		:PoolLeakDetector(_pool),
		 pool(_pool),
		 fcgi_stock(_fcgi_stock),
		 retry_timer(fcgi_stock.GetEventLoop(), BIND_THIS_METHOD(BeginConnect)),
		 stopwatch(parent_stopwatch, "fcgi", _action),
		 address(_address),
		 pending_request(_pool, _method, address.GetURI(pool),
				 std::move(_headers), std::move(_body)),
		 site_name(_site_name),
		 action(_action),
		 remote_addr(_remote_addr),
		 stderr_fd(std::move(_stderr_fd)),
		 handler(_handler),
		 /* can only retry if there is no request body */
		 retries(pending_request.body ? 0 : 2)
	{
		caller_cancel_ptr = *this;
	}

	void BeginConnect() noexcept {
		fcgi_stock.Get(address.options,
			       action, address.args.ToArray(pool),
			       address.parallelism, address.concurrency,
			       *this, cancel_ptr);
	}

private:
	void Destroy() noexcept {
		DeleteFromPool(pool, this);
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr error) noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr error) noexcept override;

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction put_action) noexcept override {
		auto &_item = *stock_item;
		stock_item = nullptr;

		/* if an operation is still in progress, Destroy()
                   will be called upon completion*/
		if (!cancel_ptr)
			Destroy();

		return _item.Put(put_action);
	}
};

void
FcgiRequest::Cancel() noexcept
{
	if (stock_item != nullptr)
		fcgi_stock_aborted(*stock_item);

	auto _cancel_ptr = std::move(cancel_ptr);

	/* if the stock item has not yet been released, Destroy() will
           be called by ReleaseLease() */
	if (stock_item == nullptr)
		Destroy();

	if (_cancel_ptr)
		_cancel_ptr.Cancel();
}

void
FcgiRequest::OnStockItemReady(StockItem &item) noexcept
{
	assert(stock_item == nullptr);
	stock_item = &item;
	cancel_ptr = {};

	stopwatch.RecordEvent("launch");

	fcgi_stock_item_set_site(*stock_item, site_name);
	fcgi_stock_item_set_uri(*stock_item, pending_request.uri);

	/* duplicate the stderr_fd to leave the original for retry */
	UniqueFileDescriptor stderr_fd2 = stderr_fd.IsDefined()
		? stderr_fd.Duplicate()
		: fcgi_stock_item_get_stderr(*stock_item);

	const char *script_filename = address.path;

	fcgi_client_request(&pool, item.GetStock().GetEventLoop(), std::move(stopwatch),
			    fcgi_stock_item_get(*stock_item),
			    FdType::FD_SOCKET,
			    *this,
			    pending_request.method, pending_request.uri,
			    script_filename,
			    address.script_name, address.path_info,
			    address.query_string,
			    address.document_root,
			    remote_addr,
			    pending_request.headers,
			    std::move(pending_request.body),
			    address.params.ToArray(pool),
			    std::move(stderr_fd2),
			    *this, cancel_ptr);
}

void
FcgiRequest::OnStockItemError(std::exception_ptr error) noexcept
{
	assert(stock_item == nullptr);

	cancel_ptr = {};

	if (retries > 0 && IsFcgiClientRetryFailure(error)) {
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
FcgiRequest::OnHttpResponse(HttpStatus status, StringMap &&_headers,
			    UnusedIstreamPtr _body) noexcept
{
	cancel_ptr = {};

	/* from here on, no retry is ever going to happen, so we don't
	   need stderr_fd anymore */
	stderr_fd.Close();

	auto &_handler = handler;

	/* if the stock item has not yet been released, Destroy() will
           be called by ReleaseLease() */
	if (stock_item == nullptr)
		Destroy();

	_handler.InvokeResponse(status, std::move(_headers), std::move(_body));
}

void
FcgiRequest::OnHttpError(std::exception_ptr error) noexcept
{
	cancel_ptr = {};

	if (retries > 0 && IsFcgiClientRetryFailure(error)) {
		/* the server has closed the connection prematurely,
		   maybe because it didn't want to get any further
		   requests on that connection.  Let's try again. */

		--retries;
		retry_timer.Schedule(std::chrono::milliseconds{20});
		return;
	}

	auto &_handler = handler;

	/* if the stock item has not yet been released, Destroy() will
           be called by ReleaseLease() */
	if (stock_item == nullptr)
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

	auto *request = NewFromPool<FcgiRequest>(*pool, *pool, *fcgi_stock, parent_stopwatch,
						 site_name, address, action, method,
						 remote_addr,
						 std::move(headers), std::move(body),
						 std::move(stderr_fd),
						 handler, cancel_ptr);
	request->BeginConnect();
}
