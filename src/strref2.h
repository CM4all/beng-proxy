/*
 * String reference struct.  Useful for taking cheap substrings of an
 * existing string.
 *
 * This is the additional library which provides more complex
 * operations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRREF2_H
#define __BENG_STRREF2_H

#include "strref.h"
#include "strutil.h"

static __attr_always_inline void
strref_ltrim(struct strref *s)
{
    while (s->length > 0 && char_is_whitespace(s->data[0])) {
        ++s->data;
        --s->length;
    }
}

static __attr_always_inline void
strref_rtrim(struct strref *s)
{
    while (s->length > 0 && char_is_whitespace(strref_last(s)))
        --s->length;
}

static __attr_always_inline void
strref_trim(struct strref *s)
{
    strref_ltrim(s);
    strref_rtrim(s);
}

static __attr_always_inline int
strref_lower_cmp(const struct strref *s, const char *p, size_t length)
{
    size_t i;

    assert(s != NULL);
    assert(s->data != NULL || s->length == 0);
    assert(p != NULL || length == 0);

    if (s->length != length)
        return 1; /* XXX -1 or 1? */

    for (i = 0; i < length; ++i) {
        char ch = char_to_lower(s->data[i]);
        if (ch < p[i])
            return -1;
        if (ch > p[i])
            return 1;
    }

    return 0;
}

static __attr_always_inline int
strref_lower_cmp_c(const struct strref *s, const char *p)
{
    assert(p != NULL);

    return strref_lower_cmp(s, p, strlen(p));
}

#define strref_lower_cmp_literal(s, l) strref_lower_cmp((s), (l), sizeof(l) - 1)

#endif
