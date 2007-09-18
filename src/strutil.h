/*
 * Common string utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRUTIL_H
#define __BENG_STRUTIL_H

#include "compiler.h"

#include <stddef.h>

static attr_always_inline int
char_is_whitespace(char ch)
{
    return ((unsigned char)ch) <= 0x20;
}

static attr_always_inline int
char_is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static attr_always_inline int
char_is_minuscule_letter(char ch)
{
    return ch >= 'a' && ch <= 'z';
}

static attr_always_inline int
char_is_capital_letter(char ch)
{
    return ch >= 'A' && ch <= 'Z';
}

static attr_always_inline int
char_is_letter(char ch)
{
    return char_is_minuscule_letter(ch) || char_is_capital_letter(ch);
}

static attr_always_inline int
char_is_alphanumeric(char ch)
{
    return char_is_letter(ch) || char_is_digit(ch);
}

static attr_always_inline char
char_to_lower(char ch)
{
    if (unlikely(char_is_capital_letter(ch)))
        return (char)(ch + 'a' - 'A');
    else
        return ch;
}

static attr_always_inline void
char_to_lower_inplace(char *ch_r)
{
    if (unlikely(char_is_capital_letter(*ch_r)))
        *ch_r += 'a' - 'A';
}

void
str_to_lower(char *s);

#endif
