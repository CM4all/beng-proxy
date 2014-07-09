/*
 * Calculate maximum cache item age.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_age.hxx"
#include "http_cache_internal.hxx"
#include "strmap.hxx"

#include <string.h>

gcc_pure
static bool
vary_exists(const char *vary, const struct strmap *request_headers,
            const char *key)
{
    assert(vary != nullptr);
    assert(key != nullptr);
    assert(*key != 0);

    return request_headers != nullptr &&
        strstr(vary, key) != nullptr &&
        strmap_get(request_headers, key) != nullptr;
}

/**
 * Returns the upper "maximum age" limit.  If the server specifies a
 * bigger maximum age, it will be clipped at this return value.
 */
gcc_pure
static time_t
http_cache_age_limit(const struct http_cache_info *info,
                     const struct strmap *request_headers)
{
    enum {
        SECOND = 1,
        MINUTE = 60 * SECOND,
        HOUR = 60 * MINUTE,
        DAY = 24 * HOUR,
        WEEK = 7 * DAY,
    };

    if (info->vary != nullptr) {
        /* if there's a "Vary" response header, we may assume that the
           response is much more volatile, and lower limits apply */

        if (vary_exists(info->vary, request_headers, "x-cm4all-beng-user") ||
            vary_exists(info->vary, request_headers, "cookie") ||
            vary_exists(info->vary, request_headers, "cookie2"))
            /* this response is specific to this one authenticated
               user, and caching it for a long time will not be
               helpful */
            return 5 * MINUTE;

        if (strstr(info->vary, "x-widgetid") != nullptr ||
            strstr(info->vary, "x-widgethref") != nullptr)
            /* this response is specific to one widget instance */
            return 30 * MINUTE;

        return HOUR;
    }

    return WEEK;
}

time_t
http_cache_calc_expires(const struct http_cache_info *info,
                        const struct strmap *request_headers)
{
    const time_t now = time(nullptr);

    time_t max_age;
    if (info->expires == (time_t)-1)
        /* there is no Expires response header; keep it in the cache
           for 1 hour, but check with If-Modified-Since */
        max_age = 3600;
    else {
        const time_t expires = info->expires;
        if (expires <= now)
            /* already expired, bail out */
            return expires;

        max_age = info->expires - now;
    }

    const time_t age_limit = http_cache_age_limit(info, request_headers);
    if (age_limit < max_age)
        max_age = age_limit;

    return now + max_age;
}
