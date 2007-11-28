/*
 * String reference struct.  Useful for taking cheap substrings of an
 * existing string.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRREF_H
#define __BENG_STRREF_H

#include "compiler.h"
#include "pool.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

struct strref {
    size_t length;
    const char *data;
};

static attr_always_inline void
strref_clear(struct strref *s)
{
    assert(s != NULL);

    s->length = 0;
#ifndef NDEBUG
    s->data = (const char*)0x02020202;
#endif
}

static attr_always_inline void
strref_set(struct strref *s, const char *p, size_t length)
{
    assert(s != NULL);
    assert(p != NULL);

    s->length = length;
    s->data = p;
}

static attr_always_inline void
strref_set_c(struct strref *s, const char *p)
{
    assert(s != NULL);
    assert(p != NULL);

    s->length = strlen(p);
    s->data = p;
}

static attr_always_inline int
strref_is_empty(const struct strref *s)
{
    assert(s != NULL);

    return s->length == 0;
}

static attr_always_inline char
strref_last(const struct strref *s)
{
    assert(s != NULL);
    assert(s->length > 0);
    assert(s->data != NULL);

    return s->data[s->length - 1];
}

static attr_always_inline char *
strref_dup(pool_t pool, const struct strref *s)
{
    assert(pool != NULL);
    assert(s != NULL);

    return p_strndup(pool, s->data, s->length);
}

static attr_always_inline int
strref_cmp(const struct strref *s,
           const char *p, size_t length)
{
    assert(s != NULL);
    assert(s->data != NULL || s->length == 0);
    assert(p != NULL || length == 0);

    return s->length == length &&
        memcmp(s->data, p, length);
}

static attr_always_inline int
strref_cmp2(const struct strref *a, const struct strref *b)
{
    assert(a != NULL);
    assert(b != NULL);

    return strref_cmp(a, b->data, b->length);
}

static attr_always_inline int
strref_ends_with_n(const struct strref *s,
                   const char *p, size_t length)
{
    assert(s != NULL);
    assert(s->data != NULL || s->length == 0);

    return s->length >= length &&
        memcmp(s->data + s->length - length, p, length) == 0;
}

#endif
