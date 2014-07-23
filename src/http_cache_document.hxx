#ifndef BENG_PROXY_HTTP_CACHE_DOCUMENT_HXX
#define BENG_PROXY_HTTP_CACHE_DOCUMENT_HXX

#include "http_cache_info.hxx"

#include <http/status.h>

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

#endif
