/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache.h"
#include "cache.h"
#include "url-stream.h"

struct http_cache {
    struct cache *cache;
    struct hstock *stock;
};


/*
 * cache_class
 *
 */

static int
http_cache_item_validate(struct cache_item *item)
{
    (void)item;
    return 1;
}

static void
http_cache_item_destroy(struct cache_item *item)
{
    (void)item;
}

static const struct cache_class http_cache_class = {
    .validate = http_cache_item_validate,
    .destroy = http_cache_item_destroy,
};


/*
 * constructor and public methods
 *
 */

struct http_cache *
http_cache_new(pool_t pool,
               struct hstock *http_client_stock)
{
    struct http_cache *cache = p_malloc(pool, sizeof(*cache));
    cache->cache = cache_new(pool, &http_cache_class);
    cache->stock = http_client_stock;
    return cache;
}

void
http_cache_close(struct http_cache *cache)
{
    cache_close(cache->cache);
}

void
http_cache_request(struct http_cache *cache,
                   pool_t pool,
                   http_method_t method, const char *url,
                   struct growing_buffer *headers, istream_t body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    url_stream_new(pool, cache->stock,
                   method, url,
                   headers, body,
                   handler, handler_ctx,
                   async_ref);
}
