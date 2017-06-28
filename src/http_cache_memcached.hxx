/*
 * Caching HTTP responses.  Memcached backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_MEMCACHED_HXX
#define BENG_PROXY_HTTP_CACHE_MEMCACHED_HXX

#include <exception>

#include <http/status.h>

struct pool;
class StringMap;
class Istream;
struct MemachedStock;
class BackgroundManager;
class CancellablePointer;
struct HttpCacheResponseInfo;
struct HttpCacheDocument;

typedef void (*http_cache_memcached_flush_t)(bool success,
                                             std::exception_ptr ep, void *ctx);

typedef void (*http_cache_memcached_get_t)(HttpCacheDocument *document,
                                           Istream *body,
                                           std::exception_ptr ep, void *ctx);

typedef void (*http_cache_memcached_put_t)(std::exception_ptr ep, void *ctx);

void
http_cache_memcached_flush(struct pool &pool, MemachedStock &stock,
                           http_cache_memcached_flush_t callback,
                           void *callback_ctx,
                           CancellablePointer &cancel_ptr);

void
http_cache_memcached_get(struct pool &pool, MemachedStock &stock,
                         struct pool &background_pool,
                         BackgroundManager &background,
                         const char *uri, StringMap &request_headers,
                         http_cache_memcached_get_t callback,
                         void *callback_ctx,
                         CancellablePointer &cancel_ptr);

void
http_cache_memcached_put(struct pool &pool, MemachedStock &stock,
                         struct pool &background_pool,
                         BackgroundManager &background,
                         const char *uri,
                         const HttpCacheResponseInfo &info,
                         const StringMap &request_headers,
                         http_status_t status,
                         const StringMap *response_headers,
                         Istream *value,
                         http_cache_memcached_put_t put, void *callback_ctx,
                         CancellablePointer &cancel_ptr);

void
http_cache_memcached_remove_uri(MemachedStock &stock,
                                struct pool &background_pool,
                                BackgroundManager &background,
                                const char *uri);

void
http_cache_memcached_remove_uri_match(MemachedStock &stock,
                                      struct pool &background_pool,
                                      BackgroundManager &background,
                                      const char *uri, StringMap &headers);

#endif
