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

#include "http_request.hxx"
#include "http_client.hxx"
#include "http_headers.hxx"
#include "http_address.hxx"
#include "HttpResponseHandler.hxx"
#include "header_writer.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "lease.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "fs/Factory.hxx"
#include "fs/SocketFilter.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/FailureManager.hxx"
#include "net/FailureRef.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "util/Compiler.h"

#include <string.h>

static constexpr Event::Duration HTTP_CONNECT_TIMEOUT =
    std::chrono::seconds(30);

class HttpRequest final
    : Cancellable, StockGetHandler, Lease, HttpResponseHandler, PoolLeakDetector {

    struct pool &pool;

    FilteredSocketBalancer &fs_balancer;

    const sticky_hash_t session_sticky;

    SocketFilterFactory *const filter_factory;

    StockItem *stock_item = nullptr;
    FailurePtr failure;

    const http_method_t method;
    const HttpAddress &address;
    HttpHeaders headers;
    UnusedHoldIstreamPtr body;

    unsigned retries;

    HttpResponseHandler &handler;
    CancellablePointer cancel_ptr;

    bool response_sent = false;

public:
    HttpRequest(struct pool &_pool,
                FilteredSocketBalancer &_fs_balancer,
                sticky_hash_t _session_sticky,
                SocketFilterFactory *_filter_factory,
                http_method_t _method,
                const HttpAddress &_address,
                HttpHeaders &&_headers,
                UnusedIstreamPtr _body,
                HttpResponseHandler &_handler,
                CancellablePointer &_cancel_ptr)
        :PoolLeakDetector(_pool),
         pool(_pool), fs_balancer(_fs_balancer),
         session_sticky(_session_sticky),
         filter_factory(_filter_factory),
         method(_method), address(_address),
         headers(std::move(_headers)), body(pool, std::move(_body)),
         /* can only retry if there is no request body */
         retries(body ? 0 : 2),
         handler(_handler)
    {
        _cancel_ptr = *this;

        if (address.host_and_port != nullptr)
            headers.Write("host", address.host_and_port);
    }

    void BeginConnect() {
        fs_balancer.Get(pool,
                        false, SocketAddress::Null(),
                        session_sticky,
                        address.addresses,
                        HTTP_CONNECT_TIMEOUT,
                        filter_factory,
                        *this, cancel_ptr);
    }

private:
    void Destroy() {
        assert(stock_item == nullptr);

        DeleteFromPool(pool, this);
    }

    void ResponseSent() {
        assert(!response_sent);
        response_sent = true;

        if (stock_item == nullptr)
            Destroy();
    }

    void Failed(std::exception_ptr ep) {
        body.Clear();
        handler.InvokeError(ep);
        ResponseSent();
    }

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        assert(!response_sent);

        /* this pool reference is necessary because
           cancel_ptr.Cancel() may release the only remaining
           reference on the pool */
        const ScopePoolRef ref(pool TRACE_ARGS);

        body.Clear();
        cancel_ptr.Cancel();

        assert(stock_item == nullptr);
        Destroy();
    }

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) noexcept override;
    void OnStockItemError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) noexcept override;

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
    assert(!response_sent);

    failure->Unset(FAILURE_PROTOCOL);

    handler.InvokeResponse(status, std::move(_headers), std::move(_body));
    ResponseSent();
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
    assert(!response_sent);

    if (retries > 0 &&
        HasHttpClientErrorCode(ep, HttpClientErrorCode::REFUSED)) {
        /* the server has closed the connection prematurely, maybe
           because it didn't want to get any further requests on that
           TCP connection.  Let's try again. */

        --retries;
        BeginConnect();
    } else {
        if (IsHttpClientServerFailure(ep)) {
            failure->Set(FAILURE_PROTOCOL,
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
    assert(stock_item == nullptr);
    assert(!response_sent);

    stock_item = &item;

    failure = fs_balancer.GetFailureManager()
        .Make(fs_stock_item_get_address(*stock_item));

    http_client_request(pool,
                        fs_stock_item_get(item),
                        *this,
                        item.GetStockName(),
                        method, address.path, std::move(headers),
                        std::move(body), true,
                        *this, cancel_ptr);
}

void
HttpRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
    assert(stock_item == nullptr);
    assert(!response_sent);

    Failed(ep);
}

/*
 * Lease
 *
 */

void
HttpRequest::ReleaseLease(bool reuse) noexcept
{
    assert(stock_item != nullptr);

    stock_item->Put(!reuse);
    stock_item = nullptr;

    if (response_sent) {
        Destroy();
    }
}

/*
 * constructor
 *
 */

void
http_request(struct pool &pool,
             FilteredSocketBalancer &fs_balancer,
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

    auto hr = NewFromPool<HttpRequest>(pool, pool, fs_balancer,
                                       session_sticky,
                                       filter_factory,
                                       method, uwa,
                                       std::move(headers), std::move(body),
                                       handler, _cancel_ptr);

    hr->BeginConnect();
}
