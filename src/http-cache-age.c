/*
 * Calculate maximum cache item age.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-age.h"
#include "http-cache-internal.h"

time_t
http_cache_calc_expires(const struct http_cache_info *info)
{
    time_t expires;
    if (info->expires == (time_t)-1)
        /* there is no Expires response header; keep it in the cache
           for 1 hour, but check with If-Modified-Since */
        expires = time(NULL) + 3600;
    else
        expires = info->expires;

    return expires;
}
