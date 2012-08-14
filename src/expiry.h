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

static inline unsigned
expiry_touch(unsigned duration)
{
    return now_s() + duration;
}

gcc_pure
static inline bool
is_expired(unsigned expires)
{
    return now_s() >= expires;
}

#endif
