/*
 * Get resources, either a static file, from a CGI program or from a
 * HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "get.hxx"
#include "resource_loader.hxx"
#include "http_cache.hxx"

void
resource_get(struct http_cache *cache,
             struct tcp_balancer *tcp_balancer,
             struct lhttp_stock *lhttp_stock,
             struct fcgi_stock *fcgi_stock,
             struct hstock *was_stock,
             struct hstock *delegate_stock,
             struct nfs_cache *nfs_cache,
             struct pool *pool,
             unsigned session_sticky,
             http_method_t method,
             const struct resource_address *address,
             http_status_t status, struct strmap *headers,
             struct istream *body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    assert(fcgi_stock != nullptr);
    assert(pool != nullptr);
    assert(address != nullptr);

    if (cache != nullptr) {
        http_cache_request(cache, pool, session_sticky,
                           method, address,
                           headers, body,
                           handler, handler_ctx, async_ref);
    } else {
        struct resource_loader *rl =
            resource_loader_new(pool, tcp_balancer,
                                lhttp_stock, fcgi_stock, was_stock,
                                delegate_stock, nfs_cache);
        resource_loader_request(rl, pool, session_sticky,
                                method, address, status, headers, body,
                                handler, handler_ctx, async_ref);
    }
}
