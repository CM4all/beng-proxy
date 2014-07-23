#ifndef BENG_PROXY_HTTP_CACHE_INFO_HXX
#define BENG_PROXY_HTTP_CACHE_INFO_HXX

#include <sys/time.h>

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
