#ifndef BENG_PROXY_HTTP_CACHE_INTERNAL_H
#define BENG_PROXY_HTTP_CACHE_INTERNAL_H

#include "http-cache.h"

#include <http/status.h>

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

#include <glib.h>
#include <sys/time.h>
#include <sys/types.h> /* for off_t */

struct pool;
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
http_cache_copy_info(struct pool *pool, struct http_cache_info *dest,
                     const struct http_cache_info *src);

struct http_cache_info *
http_cache_info_dup(struct pool *pool, const struct http_cache_info *src);

struct http_cache_info *
http_cache_request_evaluate(struct pool *pool,
                            http_method_t method,
                            const struct resource_address *address,
                            const struct strmap *headers,
                            struct istream *body);

void
http_cache_document_init(struct http_cache_document *document, struct pool *pool,
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
http_cache_copy_vary(struct pool *pool, const char *vary,
                     const struct strmap *headers);

/**
 * The server sent us a non-"Not Modified" response.  Check if we want
 * to serve the cache item anyway, and discard the server's response.
 */
bool
http_cache_prefer_cached(const struct http_cache_document *document,
                         const struct strmap *response_headers);

#endif
