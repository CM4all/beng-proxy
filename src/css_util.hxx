/*
 * CSS utility functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CSS_UTIL_HXX
#define BENG_PROXY_CSS_UTIL_HXX

#include "css_syntax.hxx"

/**
 * Count the number of leading underscores.  Returns 0 if the
 * underscores are not followed by a different name character.
 */
gcc_pure
static inline unsigned
underscore_prefix(const char *p, const char *end)
{
    const char *q = p;
    while (q < end && *q == '_')
        ++q;

    return q - p;
}

gcc_pure
static inline bool
is_underscore_prefix(const char *p, const char *end)
{
    unsigned n = underscore_prefix(p, end);
    return n == 2 || n == 3;
}

#endif
