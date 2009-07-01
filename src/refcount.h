/*
 * Reference counting API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_REFCOUNT_H
#define __BENG_REFCOUNT_H

#include <glib.h>

#include <stdbool.h>
#include <assert.h>

struct refcount {
    volatile gint value;
};

static inline void
refcount_init(struct refcount *rc)
{
    g_atomic_int_set(&rc->value, 1);
}

static inline void
refcount_get(struct refcount *rc)
{
    assert(g_atomic_int_get(&rc->value) > 0);

    g_atomic_int_inc(&rc->value);
}

/**
 * Decreases the reference counter, and returns true if the counter
 * has reached 0.
 */
static inline bool
refcount_put(struct refcount *rc)
{
    assert(rc->value > 0);

    return g_atomic_int_dec_and_test(&rc->value);
}

#endif
