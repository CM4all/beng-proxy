/*
 * Classify characters in a HTML/XML document.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef HTML_CHARS_HXX
#define HTML_CHARS_HXX

#include "util/CharUtil.hxx"

static constexpr inline bool
is_html_name_start_char(char ch)
{
    return IsAlphaASCII(ch) ||
        ch == ':' || ch == '_';
}

static constexpr inline bool
is_html_name_char(char ch)
{
    return is_html_name_start_char(ch) || IsDigitASCII(ch) ||
        ch == '-' || ch == '.';
}

#endif
