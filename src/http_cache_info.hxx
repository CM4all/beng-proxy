#ifndef BENG_PROXY_HTTP_CACHE_INFO_HXX
#define BENG_PROXY_HTTP_CACHE_INFO_HXX

#include <chrono>

struct HttpCacheRequestInfo {
    /**
     * Is the request served by a remote server?  If yes, then we
     * require the "Date" header to be present.
     */
    bool is_remote;

    bool only_if_cached = false;

    /** does the request URI have a query string?  This information is
        important for RFC 2616 13.9 */
    bool has_query_string;
};

struct HttpCacheResponseInfo {
    /** when will the cached resource expire? (beng-proxy time) */
    std::chrono::system_clock::time_point expires;

    /** when was the cached resource last modified on the widget
        server? (widget server time) */
    const char *last_modified;

    const char *etag;

    const char *vary;

    HttpCacheResponseInfo() = default;
    HttpCacheResponseInfo(struct pool &pool,
                          const HttpCacheResponseInfo &src);

    HttpCacheResponseInfo(const HttpCacheResponseInfo &) = delete;
    HttpCacheResponseInfo &operator=(const HttpCacheResponseInfo &) = delete;

    void MoveToPool(struct pool &pool);
};

#endif
