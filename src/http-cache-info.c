/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"

void
http_cache_copy_info(pool_t pool, struct http_cache_info *dest,
                     const struct http_cache_info *src)
{
    dest->expires = src->expires;

    if (src->last_modified != NULL)
        dest->last_modified = p_strdup(pool, src->last_modified);
    else
        dest->last_modified = NULL;

    if (src->etag != NULL)
        dest->etag = p_strdup(pool, src->etag);
    else
        dest->etag = NULL;

    if (src->vary != NULL)
        dest->vary = p_strdup(pool, src->vary);
    else
        dest->vary = NULL;
}

struct http_cache_info *
http_cache_info_dup(pool_t pool, const struct http_cache_info *src)
{
    struct http_cache_info *dest = p_malloc(pool, sizeof(*dest));

    http_cache_copy_info(pool, dest, src);
    return dest;
}
