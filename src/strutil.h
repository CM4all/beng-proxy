/*
 * Common string utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_STRUTIL_H
#define __BENG_STRUTIL_H

#include <inline/compiler.h>

#include <stdbool.h>

static gcc_always_inline bool
char_is_whitespace(char ch)
{
    return ((unsigned char)ch) <= 0x20;
}

static gcc_always_inline bool
char_is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static gcc_always_inline bool
char_is_minuscule_letter(char ch)
{
    return ch >= 'a' && ch <= 'z';
}

static gcc_always_inline bool
char_is_capital_letter(char ch)
{
    return ch >= 'A' && ch <= 'Z';
}

static gcc_always_inline bool
char_is_letter(char ch)
{
    return char_is_minuscule_letter(ch) || char_is_capital_letter(ch);
}

static gcc_always_inline bool
char_is_alphanumeric(char ch)
{
    return char_is_letter(ch) || char_is_digit(ch);
}

static gcc_always_inline char
char_to_lower(char ch)
{
    if (unlikely(char_is_capital_letter(ch)))
        return (char)(ch + 'a' - 'A');
    else
        return ch;
}

static gcc_always_inline char
char_to_upper(char ch)
{
    if (unlikely(char_is_minuscule_letter(ch)))
        return (char)(ch - 'a' + 'A');
    else
        return ch;
}

static gcc_always_inline void
char_to_lower_inplace(char *ch_r)
{
    if (unlikely(char_is_capital_letter(*ch_r)))
        *ch_r += 'a' - 'A';
}

static gcc_always_inline void
char_to_upper_inplace(char *ch_r)
{
    if (unlikely(char_is_minuscule_letter(*ch_r)))
        *ch_r -= 'a' - 'A';
}

void
str_to_lower(char *s);

void
str_to_upper(char *s);

#endif
