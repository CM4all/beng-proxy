#ifndef BENG_PROXY_HTTP_CACHE_INTERNAL_HXX
#define BENG_PROXY_HTTP_CACHE_INTERNAL_HXX

#include "http_cache.h"

#include <inline/compiler.h>
#include <http/status.h>

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

#include <sys/time.h>
#include <sys/types.h> /* for off_t */

struct pool;
struct growing_buffer;

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

    http_cache_info()
        :only_if_cached(false),
         expires((time_t)-1),
         last_modified(nullptr),
         etag(nullptr) {}

    http_cache_info(struct pool &pool, const http_cache_info &src);
};

struct http_cache_document {
    struct http_cache_info info;

    struct strmap *vary;

    http_status_t status;
    struct strmap *headers;

    http_cache_document() = default;
    http_cache_document(const http_cache_document &) = delete;

    http_cache_document(struct pool &pool,
                        const struct http_cache_info &_info,
                        struct strmap *request_headers,
                        http_status_t _status,
                        struct strmap *response_headers);
};

static inline void
http_cache_info_init(struct http_cache_info *info)
{
    info->only_if_cached = false;
    info->expires = (time_t)-1;
    info->last_modified = nullptr;
    info->etag = nullptr;
}

struct http_cache_info *
http_cache_info_dup(struct pool &pool, const struct http_cache_info &src);

#endif
