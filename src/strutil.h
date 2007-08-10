/*
 * Common string utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRUTIL_H
#define __BENG_STRUTIL_H

#include <stddef.h>

static inline int
char_is_whitespace(char ch)
{
    return (ch & ~0x1f) == 0;
}

void
str_to_lower(char *s);

#endif
