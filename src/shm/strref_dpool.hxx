/*
 * Allocating strref data from a distributed memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef STRREF_DPOOL_H
#define STRREF_DPOOL_H

#include "strref.h"
#include "dpool.h"

#include <inline/poison.h>

static gcc_always_inline void
strref_set_dup_d(struct dpool *pool, struct strref *dest, const struct strref *src)
{
    assert(dest != nullptr);
    assert(src != nullptr);
    assert(src->length == 0 || src->data != nullptr);

    if (src->length == 0) {
        dest->length = 0;
    } else {
        dest->data = d_memdup(pool, src->data, src->length);
        if (dest->data != nullptr)
            dest->length = src->length;
        else
            dest->length = 0;
    }
}

static gcc_always_inline char *
strref_dup_d(struct dpool *pool, const struct strref *s)
{
    assert(pool != nullptr);
    assert(s != nullptr);

    return d_strndup(pool, s->data, s->length);
}

static gcc_always_inline void
strref_free_d(struct dpool *pool, struct strref *s)
{
    assert(pool != nullptr);
    assert(s != nullptr);
    assert(s->length > 0);
    assert(s->data != nullptr);

    d_free(pool, s->data);
    poison_undefined(s, sizeof(*s));
}

#endif
