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

class FcgiRemoteRequest final : StockGetHandler, Cancellable, Lease, PoolLeakDetector {
	struct pool &pool;
	EventLoop &event_loop;

	StockItem *stock_item;

	const CgiAddress &address;

	PendingHttpRequest pending_request;

	const char *const remote_addr;

	UniqueFileDescriptor stderr_fd;

	StopwatchPtr stopwatch;

	HttpResponseHandler &handler;
	CancellablePointer &caller_cancel_ptr;
	CancellablePointer connect_cancel_ptr;

public:
	FcgiRemoteRequest(struct pool &_pool, EventLoop &_event_loop,
			  const StopwatchPtr &parent_stopwatch,
			  const CgiAddress &_address,
			  HttpMethod _method,
			  const char *_remote_addr,
			  StringMap &&_headers,
			  UnusedIstreamPtr _body,
			  UniqueFileDescriptor &&_stderr_fd,
			  HttpResponseHandler &_handler,
			  CancellablePointer &_cancel_ptr)
	:PoolLeakDetector(_pool),
	 pool(_pool), event_loop(_event_loop),
	 address(_address),
	 pending_request(_pool, _method, address.GetURI(pool),
			 std::move(_headers), std::move(_body)),
	 remote_addr(_remote_addr),
	 stderr_fd(std::move(_stderr_fd)),
	 stopwatch(parent_stopwatch, "fcgi", pending_request.uri),
	 handler(_handler), caller_cancel_ptr(_cancel_ptr) {
		caller_cancel_ptr = *this;
	}

	void Start(TcpBalancer &tcp_balancer,
		   const AddressList &address_list) noexcept {
		tcp_balancer.Get(pool,
				 stopwatch,
				 false, SocketAddress::Null(),
				 0, address_list, std::chrono::seconds(20),
				 *this, connect_cancel_ptr);
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

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override {
		auto &_item = *stock_item;
		Destroy();
		return _item.Put(action);
	}
};

void
FcgiRemoteRequest::Cancel() noexcept
{
	connect_cancel_ptr.Cancel();
	Destroy();
}

/*
 * stock callback
 *
 */

void
FcgiRemoteRequest::OnStockItemReady(StockItem &item) noexcept
{
	stock_item = &item;

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
			    std::move(pending_request.headers),
			    std::move(pending_request.body),
			    address.params.ToArray(pool),
			    std::move(stderr_fd),
			    handler,
			    caller_cancel_ptr);
}

void
FcgiRemoteRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("connect_error");

	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(ep);
}

/*
 * constructor
 *
 */

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
						      handler, *cancel_ptr);

	request->Start(*tcp_balancer, address.address_list);
}
