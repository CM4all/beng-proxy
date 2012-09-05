/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_CACHE_H
#define __BENG_HTTP_CACHE_H

#include <http/method.h>

#include <stddef.h>

struct pool;
struct istream;
struct memcached_stock;
struct http_cache;
struct resource_loader;
struct resource_address;
struct strmap;
struct http_response_handler;
struct async_operation_ref;
struct cache_stats;

struct http_cache *
http_cache_new(struct pool *pool, size_t max_size,
               struct memcached_stock *memcached_stock,
               struct resource_loader *resource_loader);

void
http_cache_close(struct http_cache *cache);

void
http_cache_get_stats(const struct http_cache *cache, struct cache_stats *data);

void
http_cache_flush(struct http_cache *cache);

/**
 * @param session_sticky a portion of the session id that is used to
 * select the worker; 0 means disable stickiness
 */
void
http_cache_request(struct http_cache *cache,
                   struct pool *pool, unsigned session_sticky,
                   http_method_t method,
                   const struct resource_address *address,
                   struct strmap *headers, struct istream *body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref);

#endif
