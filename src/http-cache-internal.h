#ifndef BENG_PROXY_HTTP_CACHE_INTERNAL_H
#define BENG_PROXY_HTTP_CACHE_INTERNAL_H

#include "http-cache.h"

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

struct growing_buffer;
struct background_manager;

static const off_t cacheable_size_limit = 256 * 1024;

struct http_cache_info {
    /**
     * Is the request served by a remote server?  If yes, then we
     * require the "Date" header to be present.
     */
    bool is_remote;

    bool only_if_cached;

    /** does the request URI have a query string?  This information is
        important for RFC 2616 13.9 */
    bool has_query_string;

    /** when will the cached resource expire? (beng-proxy time) */
    time_t expires;

    /** when was the cached resource last modified on the widget
        server? (widget server time) */
    const char *last_modified;

    const char *etag;

    const char *vary;
};

struct http_cache_document {
    struct http_cache_info info;

    struct strmap *vary;

    http_status_t status;
    struct strmap *headers;
};

static inline void
http_cache_info_init(struct http_cache_info *info)
{
    info->only_if_cached = false;
    info->expires = (time_t)-1;
    info->last_modified = NULL;
    info->etag = NULL;
}

void
http_cache_copy_info(pool_t pool, struct http_cache_info *dest,
                     const struct http_cache_info *src);

struct http_cache_info *
http_cache_info_dup(pool_t pool, const struct http_cache_info *src);

struct http_cache_info *
http_cache_request_evaluate(pool_t pool,
                            http_method_t method,
                            const struct resource_address *address,
                            const struct strmap *headers,
                            istream_t body);

void
http_cache_document_init(struct http_cache_document *document, pool_t pool,
                         const struct http_cache_info *info,
                         struct strmap *request_headers,
                         http_status_t status,
                         struct strmap *response_headers);

/**
 * Checks whether the specified cache item fits the current request.
 * This is not true if the Vary headers mismatch.
 */
bool
http_cache_document_fits(const struct http_cache_document *document,
                         const struct strmap *headers);

/**
 * Check whether the request should invalidate the existing cache.
 */
bool
http_cache_request_invalidate(http_method_t method);

/**
 * Check whether the HTTP response should be put into the cache.
 */
bool
http_cache_response_evaluate(struct http_cache_info *info,
                             http_status_t status, const struct strmap *headers,
                             off_t body_available);

/**
 * Copy all request headers mentioned in the Vary response header to a
 * new strmap.
 */
struct strmap *
http_cache_copy_vary(pool_t pool, const char *vary,
                     const struct strmap *headers);

/**
 * The server sent us a non-"Not Modified" response.  Check if we want
 * to serve the cache item anyway, and discard the server's response.
 */
bool
http_cache_prefer_cached(const struct http_cache_document *document,
                         const struct strmap *response_headers);

struct cache *
http_cache_heap_new(pool_t pool, size_t max_size);

void
http_cache_heap_free(struct cache *cache);

struct http_cache_document *
http_cache_heap_get(struct cache *cache, const char *uri,
                    struct strmap *request_headers);

void
http_cache_heap_put(struct cache *cache, pool_t pool, const char *url,
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

istream_t
http_cache_heap_istream(pool_t pool, struct cache *cache,
                        struct http_cache_document *document);

typedef void (*http_cache_memcached_flush_t)(bool success, void *ctx);

typedef void (*http_cache_memcached_get_t)(struct http_cache_document *document,
                                           istream_t body, void *ctx);

typedef void (*http_cache_memcached_put_t)(void *ctx);

void
http_cache_memcached_flush(pool_t pool, struct memcached_stock *stock,
                           http_cache_memcached_flush_t callback,
                           void *callback_ctx,
                           struct async_operation_ref *async_ref);

void
http_cache_memcached_get(pool_t pool, struct memcached_stock *stock,
                         pool_t background_pool,
                         struct background_manager *background,
                         const char *uri, struct strmap *request_headers,
                         http_cache_memcached_get_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref);

void
http_cache_memcached_put(pool_t pool, struct memcached_stock *stock,
                         pool_t background_pool,
                         struct background_manager *background,
                         const char *uri,
                         const struct http_cache_info *info,
                         struct strmap *request_headers,
                         http_status_t status, struct strmap *response_headers,
                         istream_t value,
                         http_cache_memcached_put_t put, void *callback_ctx,
                         struct async_operation_ref *async_ref);

void
http_cache_memcached_remove_uri(struct memcached_stock *stock,
                                pool_t background_pool,
                                struct background_manager *background,
                                const char *uri);

void
http_cache_memcached_remove_uri_match(struct memcached_stock *stock,
                                      pool_t background_pool,
                                      struct background_manager *background,
                                      const char *uri, struct strmap *headers);

#endif
