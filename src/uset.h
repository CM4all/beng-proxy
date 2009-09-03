/*
 * A small helper class which manages a small set of unsigned
 * integers.
 */

#ifndef BENG_PROXY_USET_H
#define BENG_PROXY_USET_H

#include <glib.h>

struct uset {
    unsigned num;
    unsigned values[64];
};

static inline void
uset_init(struct uset *u)
{
    u->num = 0;
}

/**
 * Adds the specified value.  Does nothing if the #uset is full.  Does
 * not check if the value already exists.
 */
static inline void
uset_add(struct uset *u, unsigned value)
{
    if (u->num < G_N_ELEMENTS(u->values))
        u->values[u->num++] = value;
}

static inline bool
uset_contains(const struct uset *u, unsigned value)
{
    for (unsigned i = 0; i < u->num; ++i)
        if (u->values[i] == value)
            return true;

    return false;
}

/**
 * Checks if the value exists, and if not, adds it.
 */
static inline bool
uset_contains_or_add(struct uset *u, unsigned value)
{
    if (uset_contains(u, value))
        return true;

    uset_add(u, value);
    return false;
}

#endif
