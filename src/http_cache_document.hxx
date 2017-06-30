#ifndef BENG_PROXY_HTTP_CACHE_DOCUMENT_HXX
#define BENG_PROXY_HTTP_CACHE_DOCUMENT_HXX

#include "http_cache_info.hxx"
#include "strmap.hxx"

#include "util/Compiler.h"
#include <http/status.h>

struct HttpCacheDocument {
    HttpCacheResponseInfo info;

    StringMap vary;

    http_status_t status;
    StringMap response_headers;

    explicit HttpCacheDocument(struct pool &pool)
        :vary(pool), response_headers(pool) {}

    HttpCacheDocument(struct pool &pool,
                      const HttpCacheResponseInfo &_info,
                      const StringMap &request_headers,
                      http_status_t _status,
                      const StringMap &response_headers);

    HttpCacheDocument(const HttpCacheDocument &) = delete;
    HttpCacheDocument &operator=(const HttpCacheDocument &) = delete;

    /**
     * Checks whether the specified cache item fits the current request.
     * This is not true if the Vary headers mismatch.
     */
    gcc_pure
    bool VaryFits(const StringMap *request_headers) const;
};

#endif
