/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "clock.h"

#include <time.h>

uint64_t
now_us(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) < 0)
        return 0;

    return t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
