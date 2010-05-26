/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
#include "strmap.h"

void
http_cache_document_init(struct http_cache_document *document, pool_t pool,
                         const struct http_cache_info *info,
                         struct strmap *request_headers,
                         http_status_t status,
                         struct strmap *response_headers)
{
    assert(http_status_is_valid(status));

    http_cache_copy_info(pool, &document->info, info);

    document->vary = document->info.vary != NULL
        ? http_cache_copy_vary(pool, document->info.vary, request_headers)
        : NULL;

    document->status = status;
    document->headers = response_headers != NULL
        ? strmap_dup(pool, response_headers)
        : NULL;
}
