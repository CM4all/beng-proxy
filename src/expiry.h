/*
 * Helper library for handling expiry time stamps.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef EXPIRY_H
#define EXPIRY_H

#include "clock.h"

#include <inline/compiler.h>

#include <stdbool.h>
#include <time.h>

static inline time_t
expiry_touch(time_t duration)
{
    return now_s() + duration;
}

gcc_pure
static inline bool
is_expired(time_t expires)
{
    return now_s() >= expires;
}

#endif
