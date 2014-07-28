/*
 * Caching HTTP responses.  Memcached backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_MEMCACHED_HXX
#define BENG_PROXY_HTTP_CACHE_MEMCACHED_HXX

#include "glibfwd.hxx"

#include <http/status.h>

struct async_operation_ref;
struct pool;
struct strmap;
struct istream;
struct memcached_stock;
class BackgroundManager;
struct http_cache_info;
struct http_cache_document;

typedef void (*http_cache_memcached_flush_t)(bool success,
                                             GError *error, void *ctx);

typedef void (*http_cache_memcached_get_t)(struct http_cache_document *document,
                                           struct istream *body,
                                           GError *error, void *ctx);

typedef void (*http_cache_memcached_put_t)(GError *error, void *ctx);

void
http_cache_memcached_flush(struct pool &pool, struct memcached_stock &stock,
                           http_cache_memcached_flush_t callback,
                           void *callback_ctx,
                           struct async_operation_ref &async_ref);

void
http_cache_memcached_get(struct pool &pool, struct memcached_stock &stock,
                         struct pool &background_pool,
                         BackgroundManager &background,
                         const char *uri, struct strmap *request_headers,
                         http_cache_memcached_get_t callback,
                         void *callback_ctx,
                         struct async_operation_ref &async_ref);

void
http_cache_memcached_put(struct pool &pool, struct memcached_stock &stock,
                         struct pool &background_pool,
                         BackgroundManager &background,
                         const char *uri,
                         const struct http_cache_info &info,
                         const struct strmap *request_headers,
                         http_status_t status,
                         const struct strmap *response_headers,
                         struct istream *value,
                         http_cache_memcached_put_t put, void *callback_ctx,
                         struct async_operation_ref &async_ref);

void
http_cache_memcached_remove_uri(struct memcached_stock &stock,
                                struct pool &background_pool,
                                BackgroundManager &background,
                                const char *uri);

void
http_cache_memcached_remove_uri_match(struct memcached_stock &stock,
                                      struct pool &background_pool,
                                      BackgroundManager &background,
                                      const char *uri, struct strmap *headers);

#endif
