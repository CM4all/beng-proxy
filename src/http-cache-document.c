/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
#include "http-util.h"
#include "strmap.h"
#include "growing-buffer.h"
#include "tpool.h"

/**
 * Copy all request headers mentioned in the Vary response header to a
 * new strmap.
 */
static struct strmap *
http_cache_copy_vary(pool_t pool, const char *vary,
                     const struct strmap *headers)
{
    struct strmap *dest = strmap_new(pool, 16);
    struct pool_mark mark;
    char **list;

    pool_mark(tpool, &mark);

    for (list = http_list_split(tpool, vary);
         *list != NULL; ++list) {
        const char *value = strmap_get_checked(headers, *list);
        if (value == NULL)
            value = "";
        strmap_set(dest, p_strdup(pool, *list),
                   p_strdup(pool, value));
    }

    pool_rewind(tpool, &mark);

    return dest;
}

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
