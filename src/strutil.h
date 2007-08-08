/*
 * Common string utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRUTIL_H
#define __BENG_STRUTIL_H

#include <sys/types.h>

static inline int
char_is_whitespace(char ch)
{
    return ch > 0 && ch <= 0x20;
}

void
str_to_lower(char *s);

#endif
