/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_H
#define BENG_PROXY_HTTP_CACHE_H

#include <inline/compiler.h>
#include <http/method.h>

#include <stddef.h>

struct pool;
class Istream;
struct MemachedStock;
struct resource_loader;
struct ResourceAddress;
struct strmap;
struct http_response_handler;
struct async_operation_ref;
struct AllocatorStats;
class HttpCache;

HttpCache *
http_cache_new(struct pool &pool, size_t max_size,
               MemachedStock *memcached_stock,
               struct resource_loader &resource_loader);

void
http_cache_close(HttpCache *cache);

void
http_cache_fork_cow(HttpCache &cache, bool inherit);

gcc_pure
AllocatorStats
http_cache_get_stats(const HttpCache &cache);

void
http_cache_flush(HttpCache &cache);

/**
 * @param session_sticky a portion of the session id that is used to
 * select the worker; 0 means disable stickiness
 */
void
http_cache_request(HttpCache &cache,
                   struct pool &pool, unsigned session_sticky,
                   http_method_t method,
                   const ResourceAddress &address,
                   struct strmap *headers, Istream *body,
                   const struct http_response_handler &handler,
                   void *handler_ctx,
                   struct async_operation_ref &async_ref);

#endif
