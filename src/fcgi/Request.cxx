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

#include "Request.hxx"
#include "Stock.hxx"
#include "Client.hxx"
#include "http_response.hxx"
#include "lease.hxx"
#include "tcp_stock.hxx"
#include "stock/Item.hxx"
#include "istream/istream.hxx"
#include "pool.hxx"
#include "AllocatorPtr.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cancellable.hxx"

#include <sys/socket.h>
#include <unistd.h>

class FcgiRequest final : Lease, Cancellable {
    struct pool &pool;

    StockItem *stock_item;

    CancellablePointer cancel_ptr;

public:
    FcgiRequest(struct pool &_pool, StockItem &_stock_item)
        :pool(_pool), stock_item(&_stock_item) {
    }

    void Start(EventLoop &event_loop, const char *path,
               http_method_t method, const char *uri,
               const char *script_name, const char *path_info,
               const char *query_string,
               const char *document_root,
               const char *remote_addr,
               const StringMap &headers, Istream *body,
               ConstBuffer<const char *> params,
               int stderr_fd,
               HttpResponseHandler &handler,
               CancellablePointer &caller_cancel_ptr) {
        caller_cancel_ptr = *this;

        const char *script_filename = fcgi_stock_translate_path(*stock_item, path,
                                                                pool);
        document_root = fcgi_stock_translate_path(*stock_item, document_root,
                                                  pool);

        fcgi_client_request(&pool, event_loop,
                            fcgi_stock_item_get(*stock_item),
                            fcgi_stock_item_get_domain(*stock_item) == AF_LOCAL
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
                            handler, cancel_ptr);
    }

private:
    /* virtual methods from class Cancellable */
    void Cancel() override {
        if (stock_item != nullptr)
            fcgi_stock_aborted(*stock_item);

        cancel_ptr.Cancel();
    }

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        stock_item->Put(!reuse);
        stock_item = nullptr;
    }
};

void
fcgi_request(struct pool *pool, EventLoop &event_loop,
             FcgiStock *fcgi_stock,
             const ChildOptions &options,
             const char *action,
             const char *path,
             ConstBuffer<const char *> args,
             http_method_t method, const char *uri,
             const char *script_name, const char *path_info,
             const char *query_string,
             const char *document_root,
             const char *remote_addr,
             const StringMap &headers, Istream *body,
             ConstBuffer<const char *> params,
             int stderr_fd,
             HttpResponseHandler &handler,
             CancellablePointer &cancel_ptr)
{
    if (action == nullptr)
        action = path;

    StockItem *stock_item;
    try {
        stock_item = fcgi_stock_get(fcgi_stock, options,
                                    action,
                                    args);
    } catch (...) {
        if (body != nullptr)
            body->CloseUnused();

        if (stderr_fd >= 0)
            close(stderr_fd);

        handler.InvokeError(std::current_exception());
        return;
    }

    auto request = NewFromPool<FcgiRequest>(*pool, *pool, *stock_item);


    request->Start(event_loop, path, method, uri,
                   script_name, path_info,
                   query_string, document_root, remote_addr,
                   headers, body, params, stderr_fd, handler,
                   cancel_ptr);
}
