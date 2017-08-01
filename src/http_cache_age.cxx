/*
 * Calculate maximum cache item age.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_age.hxx"
#include "http_cache_info.hxx"
#include "strmap.hxx"

static constexpr std::chrono::hours HOUR(1);
static constexpr std::chrono::hours DAY = 24 * HOUR;
static constexpr auto WEEK = 7 * DAY;

/**
 * Returns the upper "maximum age" limit.  If the server specifies a
 * bigger maximum age, it will be clipped at this return value.
 */
gcc_pure
static std::chrono::seconds
http_cache_age_limit(const StringMap &vary)
{
    if (vary.IsEmpty())
        return WEEK;

    /* if there's a "Vary" response header, we may assume that the
       response is much more volatile, and lower limits apply */

    if (vary.Contains("x-cm4all-beng-user") ||
        vary.Contains("cookie") ||
        vary.Contains("cookie2"))
        /* this response is specific to this one authenticated
           user, and caching it for a long time will not be
           helpful */
        return std::chrono::minutes(5);

    if (vary.Contains("x-widgetid") || vary.Contains("x-widgethref"))
        /* this response is specific to one widget instance */
        return std::chrono::minutes(30);

    return std::chrono::hours(1);
}

std::chrono::system_clock::time_point
http_cache_calc_expires(const HttpCacheResponseInfo &info,
                        const StringMap &vary)
{
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

    std::chrono::system_clock::duration max_age;
    if (info.expires == std::chrono::system_clock::from_time_t(-1))
        /* there is no Expires response header; keep it in the cache
           for 1 hour, but check with If-Modified-Since */
        max_age = std::chrono::hours(1);
    else {
        if (info.expires <= now)
            /* already expired, bail out */
            return info.expires;

        max_age = info.expires - now;
    }

    const std::chrono::system_clock::duration age_limit = http_cache_age_limit(vary);
    if (age_limit < max_age)
        max_age = age_limit;

    return now + max_age;
}
