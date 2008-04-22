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

#endif
