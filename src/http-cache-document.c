/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
#include "strmap.h"
#include "growing-buffer.h"

void
http_cache_document_init(struct http_cache_document *document, pool_t pool,
                         const struct http_cache_info *info,
                         struct strmap *request_headers,
                         http_status_t status,
                         struct strmap *response_headers,
                         const struct growing_buffer *body)
{
    http_cache_copy_info(pool, &document->info, info);

    document->vary = document->info.vary != NULL
        ? http_cache_copy_vary(pool, document->info.vary, request_headers)
        : NULL;

    document->status = status;
    document->headers = strmap_dup(pool, response_headers);

    document->data = growing_buffer_dup(body, pool, &document->size);
}
