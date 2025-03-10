// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Remote.hxx"
#include "Client.hxx"
#include "cgi/Address.hxx"
#include "http/PendingRequest.hxx"
#include "http/ResponseHandler.hxx"
#include "lease.hxx"
#include "tcp_stock.hxx"
#include "cluster/TcpBalancer.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

class FcgiRemoteRequest final
	: Lease, Cancellable, StockGetHandler, HttpResponseHandler, PoolLeakDetector
{
	struct pool &pool;
	EventLoop &event_loop;

	StockItem *stock_item = nullptr;

	const CgiAddress &address;

	PendingHttpRequest pending_request;

	const char *const remote_addr;

	UniqueFileDescriptor stderr_fd;

	StopwatchPtr stopwatch;

	HttpResponseHandler &handler;
	CancellablePointer cancel_ptr;

public:
	FcgiRemoteRequest(struct pool &_pool, EventLoop &_event_loop,
			  const StopwatchPtr &parent_stopwatch,
			  const CgiAddress &_address,
			  HttpMethod _method,
			  const char *_remote_addr,
			  StringMap &&_headers,
			  UnusedIstreamPtr _body,
			  UniqueFileDescriptor &&_stderr_fd,
			  HttpResponseHandler &_handler)
		:PoolLeakDetector(_pool),
		 pool(_pool), event_loop(_event_loop),
		 address(_address),
		 pending_request(_pool, _method, address.GetURI(pool),
				 std::move(_headers), std::move(_body)),
		 remote_addr(_remote_addr),
		 stderr_fd(std::move(_stderr_fd)),
		 stopwatch(parent_stopwatch, "fcgi", pending_request.uri),
		 handler(_handler) {}

	void Start(TcpBalancer &tcp_balancer,
		   const AddressList &address_list,
		   CancellablePointer &caller_cancel_ptr) noexcept
	{
		caller_cancel_ptr = *this;

		tcp_balancer.Get(pool,
				 stopwatch,
				 false, SocketAddress::Null(),
				 0, address_list, std::chrono::seconds(20),
				 *this, cancel_ptr);
	}

private:
	void Destroy() noexcept {
		DeleteFromPool(pool, this);
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr error) noexcept override;

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override {
		auto &_item = *stock_item;
		stock_item = nullptr;

		/* if an operation is still in progress, Destroy()
                   will be called upon completion*/
		if (!cancel_ptr)
			Destroy();

		return _item.Put(action);
	}
};

void
FcgiRemoteRequest::Cancel() noexcept
{
	assert(cancel_ptr);

	auto _cancel_ptr = std::move(cancel_ptr);

	/* if the stock item has not yet been released, Destroy() will
           be called by ReleaseLease() */
	if (stock_item == nullptr)
		Destroy();

	_cancel_ptr.Cancel();
}

void
FcgiRemoteRequest::OnStockItemReady(StockItem &item) noexcept
{
	assert(stock_item == nullptr);
	stock_item = &item;
	cancel_ptr = {};

	fcgi_client_request(&pool, event_loop, std::move(stopwatch),
			    tcp_stock_item_get(item),
			    tcp_stock_item_get_domain(item) == AF_LOCAL
			    ? FdType::FD_SOCKET : FdType::FD_TCP,
			    *this,
			    pending_request.method, pending_request.uri,
			    address.path,
			    address.script_name, address.path_info,
			    address.query_string,
			    address.document_root,
			    remote_addr,
			    pending_request.headers,
			    std::move(pending_request.body),
			    address.params.ToArray(pool),
			    std::move(stderr_fd),
			    *this, cancel_ptr);
}

void
FcgiRemoteRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	assert(stock_item == nullptr);
	cancel_ptr = {};

	stopwatch.RecordEvent("connect_error");

	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(ep);
}

void
FcgiRemoteRequest::OnHttpResponse(HttpStatus status, StringMap &&_headers,
				  UnusedIstreamPtr _body) noexcept
{
	cancel_ptr = {};

	auto &_handler = handler;

	/* if the stock item has not yet been released, Destroy() will
           be called by ReleaseLease() */
	if (stock_item == nullptr)
		Destroy();

	_handler.InvokeResponse(status, std::move(_headers), std::move(_body));
}

void
FcgiRemoteRequest::OnHttpError(std::exception_ptr error) noexcept
{
	cancel_ptr = {};

	auto &_handler = handler;

	/* if the stock item has not yet been released, Destroy() will
           be called by ReleaseLease() */
	if (stock_item == nullptr)
		Destroy();

	_handler.InvokeError(std::move(error));
}

void
fcgi_remote_request(struct pool *pool, EventLoop &event_loop,
		    TcpBalancer *tcp_balancer,
		    const StopwatchPtr &parent_stopwatch,
		    const CgiAddress &address,
		    HttpMethod method,
		    const char *remote_addr,
		    StringMap &&headers, UnusedIstreamPtr body,
		    UniqueFileDescriptor stderr_fd,
		    HttpResponseHandler &handler,
		    CancellablePointer &_cancel_ptr)
{
	CancellablePointer *cancel_ptr = &_cancel_ptr;

	auto request = NewFromPool<FcgiRemoteRequest>(*pool, *pool, event_loop,
						      parent_stopwatch,
						      address,
						      method,
						      remote_addr,
						      std::move(headers),
						      std::move(body),
						      std::move(stderr_fd),
						      handler);

	request->Start(*tcp_balancer, address.address_list, *cancel_ptr);
}
