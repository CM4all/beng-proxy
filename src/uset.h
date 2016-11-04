/*
 * A small helper class which manages a small set of unsigned
 * integers.
 */

#ifndef BENG_PROXY_USET_H
#define BENG_PROXY_USET_H

#include "util/StaticArray.hxx"

struct uset {
    StaticArray<unsigned, 64> values;
};

/**
 * Adds the specified value.  Does nothing if the #uset is full.  Does
 * not check if the value already exists.
 */
static inline void
uset_add(struct uset *u, unsigned value)
{
    u->values.checked_append(value);
}

gcc_pure
static inline bool
uset_contains(const struct uset *u, unsigned value)
{
    return u->values.contains(value);
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
