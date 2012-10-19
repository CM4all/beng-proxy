/*
 * Calculate maximum cache item age.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-age.h"
#include "http-cache-internal.h"

/**
 * Returns the upper "maximum age" limit.  If the server specifies a
 * bigger maximum age, it will be clipped at this return value.
 */
gcc_pure
static time_t
http_cache_age_limit(void)
{
    enum {
        SECOND = 1,
        MINUTE = 60 * SECOND,
        HOUR = 60 * MINUTE,
        DAY = 24 * HOUR,
        WEEK = 7 * DAY,
    };

    return WEEK;
}

time_t
http_cache_calc_expires(const struct http_cache_info *info)
{
    const time_t now = time(NULL);

    time_t max_age;
    if (info->expires == (time_t)-1)
        /* there is no Expires response header; keep it in the cache
           for 1 hour, but check with If-Modified-Since */
        max_age = now + 3600;
    else {
        const time_t expires = info->expires;
        if (expires <= now)
            /* already expired, bail out */
            return expires;

        max_age = info->expires - now;
    }

    const time_t age_limit = http_cache_age_limit();
    if (age_limit < max_age)
        max_age = age_limit;

    return now + max_age;
}
