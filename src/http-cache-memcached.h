/*
 * Caching HTTP responses.  Memcached backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_MEMCACHED_H
#define BENG_PROXY_HTTP_CACHE_MEMCACHED_H

#include <http/status.h>

#include <glib.h>

#include <stdbool.h>

struct async_operation_ref;
struct pool;
struct strmap;
struct istream;
struct memcached_stock;
struct background_manager;
struct http_cache_info;
struct http_cache_document;

typedef void (*http_cache_memcached_flush_t)(bool success,
                                             GError *error, void *ctx);

typedef void (*http_cache_memcached_get_t)(struct http_cache_document *document,
                                           struct istream *body,
                                           GError *error, void *ctx);

typedef void (*http_cache_memcached_put_t)(GError *error, void *ctx);

void
http_cache_memcached_flush(struct pool *pool, struct memcached_stock *stock,
                           http_cache_memcached_flush_t callback,
                           void *callback_ctx,
                           struct async_operation_ref *async_ref);

void
http_cache_memcached_get(struct pool *pool, struct memcached_stock *stock,
                         struct pool *background_pool,
                         struct background_manager *background,
                         const char *uri, struct strmap *request_headers,
                         http_cache_memcached_get_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref);

void
http_cache_memcached_put(struct pool *pool, struct memcached_stock *stock,
                         struct pool *background_pool,
                         struct background_manager *background,
                         const char *uri,
                         const struct http_cache_info *info,
                         struct strmap *request_headers,
                         http_status_t status, struct strmap *response_headers,
                         struct istream *value,
                         http_cache_memcached_put_t put, void *callback_ctx,
                         struct async_operation_ref *async_ref);

void
http_cache_memcached_remove_uri(struct memcached_stock *stock,
                                struct pool *background_pool,
                                struct background_manager *background,
                                const char *uri);

void
http_cache_memcached_remove_uri_match(struct memcached_stock *stock,
                                      struct pool *background_pool,
                                      struct background_manager *background,
                                      const char *uri, struct strmap *headers);

#endif
