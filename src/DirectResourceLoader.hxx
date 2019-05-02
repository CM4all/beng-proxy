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

#ifndef BENG_PROXY_DIRECT_RESOURCE_LOADER_HXX
#define BENG_PROXY_DIRECT_RESOURCE_LOADER_HXX

#include "ResourceLoader.hxx"

class EventLoop;
class SpawnService;
class StockMap;
class LhttpStock;
struct FcgiStock;
class NfsCache;
class TcpBalancer;
class FilteredSocketBalancer;

/**
 * A #ResourceLoader implementation which integrates all client-side
 * protocols implemented by beng-proxy.
 */
class DirectResourceLoader final : public ResourceLoader {
    EventLoop &event_loop;
    TcpBalancer *tcp_balancer;
    FilteredSocketBalancer &fs_balancer;
    SpawnService &spawn_service;
    LhttpStock *lhttp_stock;
    FcgiStock *fcgi_stock;
    StockMap *was_stock;
    StockMap *delegate_stock;
    NfsCache *nfs_cache;

public:
    DirectResourceLoader(EventLoop &_event_loop,
                         TcpBalancer *_tcp_balancer,
                         FilteredSocketBalancer &_fs_balancer,
                         SpawnService &_spawn_service,
                         LhttpStock *_lhttp_stock,
                         FcgiStock *_fcgi_stock, StockMap *_was_stock,
                         StockMap *_delegate_stock,
                         NfsCache *_nfs_cache) noexcept
        :event_loop(_event_loop),
         tcp_balancer(_tcp_balancer),
         fs_balancer(_fs_balancer),
         spawn_service(_spawn_service),
         lhttp_stock(_lhttp_stock),
         fcgi_stock(_fcgi_stock), was_stock(_was_stock),
         delegate_stock(_delegate_stock),
         nfs_cache(_nfs_cache)
    {
    }

    /* virtual methods from class ResourceLoader */
    void SendRequest(struct pool &pool,
                     sticky_hash_t session_sticky,
                     const char *cache_tag,
                     const char *site_name,
                     http_method_t method,
                     const ResourceAddress &address,
                     http_status_t status, StringMap &&headers,
                     UnusedIstreamPtr body, const char *body_etag,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr) noexcept override;
};

#endif
