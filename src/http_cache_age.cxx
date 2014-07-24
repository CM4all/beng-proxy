/*
 * Calculate maximum cache item age.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_age.hxx"
#include "http_cache_info.hxx"
#include "strmap.hxx"

static constexpr time_t SECOND = 1;
static constexpr time_t MINUTE = 60 * SECOND;
static constexpr time_t HOUR = 60 * MINUTE;
static constexpr time_t DAY = 24 * HOUR;
static constexpr time_t WEEK = 7 * DAY;

/**
 * Returns the upper "maximum age" limit.  If the server specifies a
 * bigger maximum age, it will be clipped at this return value.
 */
gcc_pure
static time_t
http_cache_age_limit(const struct strmap *vary)
{
    if (vary == nullptr)
        return WEEK;

    /* if there's a "Vary" response header, we may assume that the
       response is much more volatile, and lower limits apply */

    if (vary->Contains("x-cm4all-beng-user") ||
        vary->Contains("cookie") ||
        vary->Contains("cookie2"))
        /* this response is specific to this one authenticated
           user, and caching it for a long time will not be
           helpful */
        return 5 * MINUTE;

    if (vary->Contains("x-widgetid") || vary->Contains("x-widgethref"))
        /* this response is specific to one widget instance */
        return 30 * MINUTE;

    return HOUR;
}

time_t
http_cache_calc_expires(const struct http_cache_info *info,
                        const struct strmap *vary)
{
    const time_t now = time(nullptr);

    time_t max_age;
    if (info->expires == (time_t)-1)
        /* there is no Expires response header; keep it in the cache
           for 1 hour, but check with If-Modified-Since */
        max_age = HOUR;
    else {
        const time_t expires = info->expires;
        if (expires <= now)
            /* already expired, bail out */
            return expires;

        max_age = info->expires - now;
    }

    const time_t age_limit = http_cache_age_limit(vary);
    if (age_limit < max_age)
        max_age = age_limit;

    return now + max_age;
}
