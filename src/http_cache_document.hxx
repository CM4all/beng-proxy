#ifndef BENG_PROXY_HTTP_CACHE_DOCUMENT_HXX
#define BENG_PROXY_HTTP_CACHE_DOCUMENT_HXX

#include "http_cache_info.hxx"
#include "strmap.hxx"

#include <inline/compiler.h>
#include <http/status.h>

struct HttpCacheDocument {
    HttpCacheResponseInfo info;

    struct strmap vary;

    http_status_t status;
    struct strmap *response_headers;

    explicit HttpCacheDocument(struct pool &pool)
        :vary(pool) {}

    HttpCacheDocument(struct pool &pool,
                      const HttpCacheResponseInfo &_info,
                      const struct strmap *request_headers,
                      http_status_t _status,
                      const struct strmap *response_headers);

    HttpCacheDocument(const HttpCacheDocument &) = delete;
    HttpCacheDocument &operator=(const HttpCacheDocument &) = delete;

    /**
     * Checks whether the specified cache item fits the current request.
     * This is not true if the Vary headers mismatch.
     */
    gcc_pure
    bool VaryFits(const struct strmap *request_headers) const;
};

#endif
