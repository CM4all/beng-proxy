/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_internal.hxx"
#include "pool.h"

void
http_cache_copy_info(struct pool *pool, struct http_cache_info *dest,
                     const struct http_cache_info *src)
{
    dest->expires = src->expires;

    if (src->last_modified != nullptr)
        dest->last_modified = p_strdup(pool, src->last_modified);
    else
        dest->last_modified = nullptr;

    if (src->etag != nullptr)
        dest->etag = p_strdup(pool, src->etag);
    else
        dest->etag = nullptr;

    if (src->vary != nullptr)
        dest->vary = p_strdup(pool, src->vary);
    else
        dest->vary = nullptr;
}

struct http_cache_info *
http_cache_info_dup(struct pool *pool, const struct http_cache_info *src)
{
    auto dest = PoolAlloc<http_cache_info>(pool);

    http_cache_copy_info(pool, dest, src);
    return dest;
}
