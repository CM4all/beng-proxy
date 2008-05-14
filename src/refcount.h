/*
 * Reference counting API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_REFCOUNT_H
#define __BENG_REFCOUNT_H

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

static inline unsigned
refcount_put(struct refcount *rc)
{
    assert(rc->value > 0);

    return --rc->value;
}

#endif
