/*
 * Helper library for handling expiry time stamps.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef EXPIRY_H
#define EXPIRY_H

#include <stdbool.h>
#include <time.h>

static inline time_t
expiry_touch(time_t duration)
{
    int ret;
    struct timespec now;

    ret = clock_gettime(CLOCK_MONOTONIC, &now);
    if (ret < 0)
        /* system call failed - try to do something not too stupid */
        return 0;

    return now.tv_sec + duration;
}

static inline bool
is_expired(time_t expires)
{
    int ret;
    struct timespec now;

    ret = clock_gettime(CLOCK_MONOTONIC, &now);
    if (ret < 0)
        /* system call failed - try to do something not too stupid */
        return true;

    return now.tv_sec >= expires;
}

#endif
