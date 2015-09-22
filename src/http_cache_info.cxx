/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_info.hxx"
#include "pool.hxx"

HttpCacheResponseInfo::HttpCacheResponseInfo(struct pool &pool,
                                             const HttpCacheResponseInfo &src)
    :expires(src.expires),
     last_modified(p_strdup_checked(&pool, src.last_modified)),
     etag(p_strdup_checked(&pool, src.etag)),
     vary(p_strdup_checked(&pool, src.vary))
{
}

void
HttpCacheResponseInfo::MoveToPool(struct pool &pool)
{
    last_modified = p_strdup_checked(&pool, last_modified);
    etag = p_strdup_checked(&pool, etag);
    vary = p_strdup_checked(&pool, vary);
}
