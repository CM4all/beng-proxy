/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_info.hxx"
#include "pool.hxx"

http_cache_info::http_cache_info(struct pool &pool,
                                 const http_cache_info &src)
    :expires(src.expires),
     last_modified(p_strdup_checked(&pool, src.last_modified)),
     etag(p_strdup_checked(&pool, src.etag)),
     vary(p_strdup_checked(&pool, src.vary))
{
}

struct http_cache_info *
http_cache_info_dup(struct pool &pool, const struct http_cache_info &src)
{
    return NewFromPool<http_cache_info>(pool, pool, src);
}
