/*
 * Calculate maximum cache item age.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-age.h"
#include "http-cache-internal.h"
#include "strmap.h"

#include <string.h>

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

    if (info->vary != NULL) {
        /* if there's a "Vary" response header, we may assume that the
           response is much more volatile, and lower limits apply */

        if (request_headers != NULL &&
            strstr(info->vary, "x-cm4all-beng-user") != NULL &&
            strmap_get(request_headers, "x-cm4all-beng-user") != NULL)
            /* this response is specific to this one authenticated
               user, and caching it for a long time will not be
               helpful */
            return 5 * MINUTE;

        if (strstr(info->vary, "x-widgetid") != NULL ||
            strstr(info->vary, "x-widgethref") != NULL)
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

    const time_t age_limit = http_cache_age_limit(info, request_headers);
    if (age_limit < max_age)
        max_age = age_limit;

    return now + max_age;
}
