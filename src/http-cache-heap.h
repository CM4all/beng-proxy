/*
 * Caching HTTP responses in heap memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_HEAP_H
#define BENG_PROXY_HTTP_CACHE_HEAP_H

#include <http/status.h>

#include <stddef.h>

struct pool;
struct strmap;
struct cache_stats;
struct growing_buffer;
struct http_cache_info;

struct cache *
http_cache_heap_new(struct pool *pool, size_t max_size);

void
http_cache_heap_free(struct cache *cache);

void
http_cache_heap_get_stats(const struct cache *cache,
                          struct cache_stats *data);

struct http_cache_document *
http_cache_heap_get(struct cache *cache, const char *uri,
                    struct strmap *request_headers);

void
http_cache_heap_put(struct cache *cache, struct pool *pool, const char *url,
                    const struct http_cache_info *info,
                    struct strmap *request_headers,
                    http_status_t status,
                    struct strmap *response_headers,
                    const struct growing_buffer *body);

void
http_cache_heap_remove(struct cache *cache, const char *url,
                       struct http_cache_document *document);

void
http_cache_heap_remove_url(struct cache *cache, const char *url,
                           struct strmap *headers);

void
http_cache_heap_flush(struct cache *cache);

void
http_cache_heap_lock(struct http_cache_document *document);

void
http_cache_heap_unlock(struct cache *cache,
                       struct http_cache_document *document);

struct istream *
http_cache_heap_istream(struct pool *pool, struct cache *cache,
                        struct http_cache_document *document);

#endif
