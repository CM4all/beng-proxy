/*
 * Get resources, either a static file, from a CGI program or from a
 * HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "get.hxx"
#include "http_cache.hxx"

void
resource_get(HttpCache *cache,
             struct pool *pool,
             unsigned session_sticky,
             http_method_t method,
             const ResourceAddress *address,
             struct strmap *headers,
             Istream *body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    assert(cache != nullptr);
    assert(pool != nullptr);
    assert(address != nullptr);

    http_cache_request(*cache, *pool, session_sticky,
                       method, *address,
                       headers, body,
                       *handler, handler_ctx, *async_ref);
}
