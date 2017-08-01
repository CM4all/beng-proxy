/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DIRECT_RESOURCE_LOADER_HXX
#define BENG_PROXY_DIRECT_RESOURCE_LOADER_HXX

#include "ResourceLoader.hxx"

class EventLoop;
class SpawnService;
class StockMap;
class LhttpStock;
struct FcgiStock;
struct NfsCache;
struct TcpBalancer;

/**
 * A #ResourceLoader implementation which integrates all client-side
 * protocols implemented by beng-proxy.
 */
class DirectResourceLoader final : public ResourceLoader {
    EventLoop &event_loop;
    TcpBalancer *tcp_balancer;
    SpawnService &spawn_service;
    LhttpStock *lhttp_stock;
    FcgiStock *fcgi_stock;
    StockMap *was_stock;
    StockMap *delegate_stock;
    NfsCache *nfs_cache;

public:
    DirectResourceLoader(EventLoop &_event_loop,
                         TcpBalancer *_tcp_balancer,
                         SpawnService &_spawn_service,
                         LhttpStock *_lhttp_stock,
                         FcgiStock *_fcgi_stock, StockMap *_was_stock,
                         StockMap *_delegate_stock,
                         NfsCache *_nfs_cache)
        :event_loop(_event_loop),
         tcp_balancer(_tcp_balancer),
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
                     http_method_t method,
                     const ResourceAddress &address,
                     http_status_t status, StringMap &&headers,
                     Istream *body, const char *body_etag,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr) override;
};

#endif
