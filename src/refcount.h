/*
 * Reference counting API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_REFCOUNT_H
#define __BENG_REFCOUNT_H

#include <stdbool.h>
#include <assert.h>

struct refcount {
    unsigned value;
};

static inline void
refcount_init(struct refcount *rc)
{
    rc->value = 1;
}

static inline void
refcount_get(struct refcount *rc)
{
    assert(rc->value > 0);

    ++rc->value;
}

/**
 * Decreases the reference counter, and returns true if the counter
 * has reached 0.
 */
static inline bool
refcount_put(struct refcount *rc)
{
    assert(rc->value > 0);

    return --rc->value == 0;
}

#endif
