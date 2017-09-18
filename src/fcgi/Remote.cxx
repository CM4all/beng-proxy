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
#include "http_response.hxx"
#include "lease.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "abort_close.hxx"
#include "address_list.hxx"
#include "pool.hxx"
#include "strmap.hxx"
#include "istream/istream.hxx"
#include "istream/istream_hold.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cancellable.hxx"

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct FcgiRemoteRequest final : StockGetHandler, Lease {
    struct pool &pool;
    EventLoop &event_loop;

    StockItem *stock_item;

    const http_method_t method;
    const char *const uri;
    const char *const script_filename;
    const char *const script_name;
    const char *const path_info;
    const char *const query_string;
    const char *const document_root;
    const char *const remote_addr;
    const StringMap headers;
    Istream *body;

    const ConstBuffer<const char *> params;

    const int stderr_fd;

    HttpResponseHandler &handler;
    CancellablePointer &cancel_ptr;

    FcgiRemoteRequest(struct pool &_pool, EventLoop &_event_loop,
                      http_method_t _method, const char *_uri,
                      const char *_script_filename,
                      const char *_script_name, const char *_path_info,
                      const char *_query_string,
                      const char *_document_root,
                      const char *_remote_addr,
                      StringMap &&_headers,
                      ConstBuffer<const char *> _params,
                      int _stderr_fd,
                      HttpResponseHandler &_handler,
                      CancellablePointer &_cancel_ptr)
        :pool(_pool), event_loop(_event_loop),
         method(_method), uri(_uri),
         script_filename(_script_filename), script_name(_script_name),
         path_info(_path_info), query_string(_query_string),
         document_root(_document_root),
         remote_addr(_remote_addr),
         headers(std::move(_headers)),
         params(_params),
         stderr_fd(_stderr_fd),
         handler(_handler), cancel_ptr(_cancel_ptr) {
    }

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(std::exception_ptr ep) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        stock_item->Put(!reuse);
    }
};

/*
 * stock callback
 *
 */

void
FcgiRemoteRequest::OnStockItemReady(StockItem &item)
{
    stock_item = &item;

    fcgi_client_request(&pool, event_loop,
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
                        headers, body,
                        params,
                        stderr_fd,
                        handler,
                        cancel_ptr);
}

void
FcgiRemoteRequest::OnStockItemError(std::exception_ptr ep)
{
    if (stderr_fd >= 0)
        close(stderr_fd);

    handler.InvokeError(ep);
}

/*
 * constructor
 *
 */

void
fcgi_remote_request(struct pool *pool, EventLoop &event_loop,
                    TcpBalancer *tcp_balancer,
                    const AddressList *address_list,
                    const char *path,
                    http_method_t method, const char *uri,
                    const char *script_name, const char *path_info,
                    const char *query_string,
                    const char *document_root,
                    const char *remote_addr,
                    StringMap &&headers, Istream *body,
                    ConstBuffer<const char *> params,
                    int stderr_fd,
                    HttpResponseHandler &handler,
                    CancellablePointer &_cancel_ptr)
{
    CancellablePointer *cancel_ptr = &_cancel_ptr;

    auto request = NewFromPool<FcgiRemoteRequest>(*pool, *pool, event_loop,
                                                  method, uri, path,
                                                  script_name, path_info,
                                                  query_string, document_root,
                                                  remote_addr,
                                                  std::move(headers), params,
                                                  stderr_fd,
                                                  handler, *cancel_ptr);

    if (body != nullptr) {
        request->body = istream_hold_new(*pool, *body);
        cancel_ptr = &async_close_on_abort(*pool, *request->body, *cancel_ptr);
    } else
        request->body = nullptr;

    tcp_balancer->Get(*pool,
                      false, SocketAddress::Null(),
                      0, *address_list, 20,
                      *request, *cancel_ptr);
}
