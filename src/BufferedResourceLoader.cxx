/*
 * Copyright 2007-2018 Content Management AG
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

#include "BufferedResourceLoader.hxx"
#include "HttpResponseHandler.hxx"
#include "strmap.hxx"
#include "istream/BufferedIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"

class BufferedResourceLoader::Request final
    : Cancellable, BufferedIstreamHandler
{
    struct pool &pool;
    ResourceLoader &next;
    const sticky_hash_t session_sticky;
    const char *const site_name;
    const http_method_t method;
    const ResourceAddress &address;
    const http_status_t status;
    StringMap headers;
    const char *const body_etag;
    HttpResponseHandler &handler;

    CancellablePointer cancel_ptr;

public:
    Request(struct pool &_pool, ResourceLoader &_next,
            sticky_hash_t _session_sticky, const char *_site_name,
            http_method_t _method, const ResourceAddress &_address,
            http_status_t _status, StringMap &&_headers,
            const char *_body_etag,
            HttpResponseHandler &_handler,
            CancellablePointer &_cancel_ptr) noexcept
        :pool(_pool), next(_next),
         session_sticky(_session_sticky), site_name(_site_name),
         method(_method), address(_address),
         status(_status),
         /* copy the headers, because they may come from a
            FilterCacheRequest pool which may be freed before
            BufferedIstream becomes ready */
         headers(pool, _headers),
         body_etag(_body_etag),
         handler(_handler)
    {
        _cancel_ptr = *this;
    }

    void Start(EventLoop &_event_loop, PipeStock *_pipe_stock,
               UnusedIstreamPtr &&body) noexcept {
        NewBufferedIstream(pool, _event_loop, _pipe_stock,
                           *this, std::move(body),
                           cancel_ptr);
    }

private:
    void Destroy() noexcept {
        this->~Request();
    }

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        auto c = std::move(cancel_ptr);
        Destroy();
        c.Cancel();
    }

    /* virtual methods from class BufferedIstreamHandler */
    void OnBufferedIstreamReady(UnusedIstreamPtr i) noexcept override {
        next.SendRequest(pool, session_sticky, site_name,
                         method, address, status, std::move(headers),
                         std::move(i), body_etag,
                         handler, cancel_ptr);
    }

    void OnBufferedIstreamError(std::exception_ptr e) noexcept override {
        handler.InvokeError(std::move(e));
    }
};

void
BufferedResourceLoader::SendRequest(struct pool &pool,
                                    sticky_hash_t session_sticky,
                                    const char *site_name,
                                    http_method_t method,
                                    const ResourceAddress &address,
                                    http_status_t status, StringMap &&headers,
                                    UnusedIstreamPtr body, const char *body_etag,
                                    HttpResponseHandler &handler,
                                    CancellablePointer &cancel_ptr) noexcept
{
    if (body) {
        auto *request = NewFromPool<Request>(pool, pool, next,
                                             session_sticky, site_name,
                                             method, address,
                                             status, std::move(headers),
                                             body_etag, handler, cancel_ptr);
        request->Start(event_loop, pipe_stock, std::move(body));
    } else {
        next.SendRequest(pool, session_sticky, site_name,
                         method, address, status, std::move(headers),
                         std::move(body), body_etag,
                         handler, cancel_ptr);
    }
}
