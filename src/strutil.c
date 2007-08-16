/*
 * Common string utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "strutil.h"
#include "compiler.h"

static inline int
is_upcase(char ch)
{
    return ch >= 'A' && ch <= 'Z';
}

void
str_to_lower(char *s)
{
    for (; *s != 0; ++s)
        if (unlikely(is_upcase(*s)))
            *s += 'a' - 'A';
}
