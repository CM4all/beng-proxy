/*
 * Common string utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRUTIL_H
#define __BENG_STRUTIL_H

#include "compiler.h"

#include <stddef.h>

static inline int
char_is_whitespace(char ch)
{
    return (ch & ~0x1f) == 0;
}

static inline int
char_is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

void
str_to_lower(char *s);

#endif
