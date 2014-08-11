/*
 * CSS syntax rules.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CSS_SYNTAX_HXX
#define BENG_PROXY_CSS_SYNTAX_HXX

#include "util/CharUtil.hxx"

static inline constexpr bool
is_css_nonascii(char ch)
{
    return !IsASCII(ch);
}

static inline bool
is_css_nmstart(char ch)
{
    return ch == '_' || IsAlphaASCII(ch) || is_css_nonascii(ch) ||
        ch == '\\';
}

static inline bool
is_css_nmchar(char ch)
{
    return is_css_nmstart(ch) || IsDigitASCII(ch) || ch == '-';
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
