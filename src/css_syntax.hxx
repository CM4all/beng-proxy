/*
 * CSS syntax rules.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CSS_SYNTAX_HXX
#define BENG_PROXY_CSS_SYNTAX_HXX

#include "strutil.h"

static inline constexpr bool
is_css_nonascii(char ch)
{
    return (ch & 0x80) != 0;
}

static inline bool
is_css_nmstart(char ch)
{
    return ch == '_' || char_is_letter(ch) || is_css_nonascii(ch) ||
        ch == '\\';
}

static inline bool
is_css_nmchar(char ch)
{
    return is_css_nmstart(ch) || char_is_digit(ch) || ch == '-';
}

static inline bool
is_css_ident_start(char ch)
{
    return ch == '-' || is_css_nmstart(ch);
}

static inline bool
is_css_ident_char(char ch)
{
    return is_css_nmchar(ch);
}

#endif
