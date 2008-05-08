/*
 * Allocating strref data from a distributed memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRREF_DPOOL_H
#define __BENG_STRREF_DPOOL_H

#include "strref.h"
#include "dpool.h"

#include <inline/poison.h>

static __attr_always_inline void
strref_set_dup_d(struct dpool *pool, struct strref *dest, const struct strref *src)
{
    assert(dest != NULL);
    assert(src != NULL);
    assert(src->length == 0 || src->data != NULL);

    if (src->length == 0) {
        dest->length = 0;
    } else {
        dest->data = d_memdup(pool, src->data, src->length);
        if (dest->data != NULL)
            dest->length = src->length;
        else
            dest->length = 0;
    }
}

static __attr_always_inline char *
strref_dup_d(struct dpool *pool, const struct strref *s)
{
    assert(pool != NULL);
    assert(s != NULL);

    return d_strndup(pool, s->data, s->length);
}

static __attr_always_inline void
strref_free_d(struct dpool *pool, struct strref *s)
{
    assert(pool != NULL);
    assert(s != NULL);
    assert(s->length > 0);
    assert(s->data != NULL);

    d_free(pool, s->data);
    poison_undefined(s, sizeof(*s));
}

#endif
