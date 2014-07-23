#ifndef BENG_PROXY_HTTP_CACHE_INTERNAL_HXX
#define BENG_PROXY_HTTP_CACHE_INTERNAL_HXX

#include "http_cache.h"
#include "http_cache_info.hxx"

#include <inline/compiler.h>
#include <http/status.h>

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

#include <sys/types.h> /* for off_t */

struct pool;
struct growing_buffer;

static const off_t cacheable_size_limit = 256 * 1024;

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
