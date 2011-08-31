/*
 * CSS utility functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CSS_UTIL_H
#define BENG_PROXY_CSS_UTIL_H

#include "css_syntax.h"

/**
 * Count the number of leading underscores.  Returns 0 if the
 * underscores are not followed by a different name character.
 */
static inline unsigned
underscore_prefix(const char *p, const char *end)
{
    const char *q = p;
    while (q < end) {
        if (*q == '_')
            ++q;
        else if (is_css_ident_char(*q))
            return q - p;
        else
            /* only underscores */
            return 0;
    }

    /* only underscores (or empty) */
    return 0;
}

static inline bool
is_underscore_prefix(const char *p, const char *end)
{
    unsigned n = underscore_prefix(p, end);
    return n == 2 || n == 3;
}

#endif
