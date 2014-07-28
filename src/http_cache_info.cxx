/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_info.hxx"
#include "pool.hxx"

http_cache_response_info::http_cache_response_info(struct pool &pool,
                                                   const http_cache_response_info &src)
    :expires(src.expires),
     last_modified(p_strdup_checked(&pool, src.last_modified)),
     etag(p_strdup_checked(&pool, src.etag)),
     vary(p_strdup_checked(&pool, src.vary))
{
}

void
http_cache_response_info::MoveToPool(struct pool &pool)
{
    last_modified = p_strdup_checked(&pool, last_modified);
    etag = p_strdup_checked(&pool, etag);
    vary = p_strdup_checked(&pool, vary);
}
