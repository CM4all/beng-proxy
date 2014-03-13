/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_internal.hxx"
#include "strmap.h"

void
http_cache_document_init(struct http_cache_document *document,
                         struct pool *pool,
                         const struct http_cache_info *info,
                         struct strmap *request_headers,
                         http_status_t status,
                         struct strmap *response_headers)
{
    assert(http_status_is_valid(status));

    http_cache_copy_info(pool, &document->info, info);

    document->vary = document->info.vary != nullptr
        ? http_cache_copy_vary(pool, document->info.vary, request_headers)
        : nullptr;

    document->status = status;
    document->headers = response_headers != nullptr
        ? strmap_dup(pool, response_headers, 7)
        : nullptr;
}
