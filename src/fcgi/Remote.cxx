/*
 * Copyright 2007-2017 Content Management AG
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

#include "Remote.hxx"
#include "Client.hxx"
#include "HttpResponseHandler.hxx"
#include "lease.hxx"
#include "tcp_stock.hxx"
#include "cluster/TcpBalancer.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "strmap.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

class FcgiRemoteRequest final : StockGetHandler, Cancellable, Lease, PoolLeakDetector {
	struct pool &pool;
	EventLoop &event_loop;

	StopwatchPtr stopwatch;

	StockItem *stock_item;

	const http_method_t method;
	const char *const uri;
	const char *const script_filename;
	const char *const script_name;
	const char *const path_info;
	const char *const query_string;
	const char *const document_root;
	const char *const remote_addr;
	StringMap headers;

	UnusedHoldIstreamPtr body;

	const ConstBuffer<const char *> params;

	UniqueFileDescriptor stderr_fd;

	HttpResponseHandler &handler;
	CancellablePointer &caller_cancel_ptr;
	CancellablePointer connect_cancel_ptr;

public:
	FcgiRemoteRequest(struct pool &_pool, EventLoop &_event_loop,
			  const StopwatchPtr &parent_stopwatch,
			  http_method_t _method, const char *_uri,
			  const char *_script_filename,
			  const char *_script_name, const char *_path_info,
			  const char *_query_string,
			  const char *_document_root,
			  const char *_remote_addr,
			  StringMap &&_headers,
			  UnusedIstreamPtr _body,
			  ConstBuffer<const char *> _params,
			  UniqueFileDescriptor &&_stderr_fd,
			  HttpResponseHandler &_handler,
			  CancellablePointer &_cancel_ptr)
	:PoolLeakDetector(_pool),
	 pool(_pool), event_loop(_event_loop),
	 stopwatch(parent_stopwatch, "fcgi", _uri),
	 method(_method), uri(_uri),
	 script_filename(_script_filename), script_name(_script_name),
	 path_info(_path_info), query_string(_query_string),
	 document_root(_document_root),
	 remote_addr(_remote_addr),
	 headers(std::move(_headers)),
	 body(pool, std::move(_body)),
	 params(_params),
	 stderr_fd(std::move(_stderr_fd)),
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
	void Cancel() noexcept override {
		connect_cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class Lease */
	void ReleaseLease(bool reuse) noexcept override {
		stock_item->Put(!reuse);
		Destroy();
	}
};

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
			    method, uri,
			    script_filename,
			    script_name, path_info,
			    query_string,
			    document_root,
			    remote_addr,
			    std::move(headers), std::move(body),
			    params,
			    std::move(stderr_fd),
			    handler,
			    caller_cancel_ptr);
}

void
FcgiRemoteRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("connect_error");

	if (stderr_fd.IsDefined())
		stderr_fd.Close();

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
		    const AddressList *address_list,
		    const char *path,
		    http_method_t method, const char *uri,
		    const char *script_name, const char *path_info,
		    const char *query_string,
		    const char *document_root,
		    const char *remote_addr,
		    StringMap &&headers, UnusedIstreamPtr body,
		    ConstBuffer<const char *> params,
		    UniqueFileDescriptor stderr_fd,
		    HttpResponseHandler &handler,
		    CancellablePointer &_cancel_ptr)
{
	CancellablePointer *cancel_ptr = &_cancel_ptr;

	auto request = NewFromPool<FcgiRemoteRequest>(*pool, *pool, event_loop,
						      parent_stopwatch,
						      method, uri, path,
						      script_name, path_info,
						      query_string, document_root,
						      remote_addr,
						      std::move(headers),
						      std::move(body),
						      params,
						      std::move(stderr_fd),
						      handler, *cancel_ptr);

	request->Start(*tcp_balancer, *address_list);
}
