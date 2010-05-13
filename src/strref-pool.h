/*
 * Allocating strref data from a memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRREF_POOL_H
#define __BENG_STRREF_POOL_H

#include "strref.h"
#include "pool.h"

#include <inline/poison.h>

static __attr_always_inline char *
strref_set_dup(pool_t pool, struct strref *dest, const struct strref *src)
{
    assert(dest != NULL);
    assert(src != NULL);
    assert(src->length == 0 || src->data != NULL);

    if (src->length == 0) {
        dest->length = 0;
        return NULL;
    } else {
        dest->length = src->length;
        char *p = p_memdup(pool, src->data, src->length);
        dest->data = p;
        return p;
    }
}

static __attr_always_inline void
strref_append(pool_t pool, struct strref *dest, const struct strref *src)
{
    char *p;

    assert(dest != NULL);
    assert(src != NULL);
    assert(src->length == 0 || src->data != NULL);

    if (src->length == 0)
        return;

    p = p_malloc(pool, dest->length + src->length);
    memcpy(p, dest->data, dest->length);
    memcpy(p + dest->length, src->data, src->length);

    dest->data = p;
    dest->length += src->length;
}

static __attr_always_inline char *
strref_dup(pool_t pool, const struct strref *s)
{
    assert(pool != NULL);
    assert(s != NULL);

    return p_strndup(pool, s->data, s->length);
}

static __attr_always_inline void
strref_free(pool_t pool, struct strref *s)
{
    assert(pool != NULL);
    assert(s != NULL);
    assert(s->length > 0);
    assert(s->data != NULL);

    p_free(pool, s->data);
    poison_undefined(s, sizeof(*s));
}

#endif
