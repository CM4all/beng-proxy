/*
 * Classify characters in a HTML/XML document.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTML_CHARS_H
#define BENG_PROXY_HTML_CHARS_H

#include "strutil.h"

static inline bool
is_html_name_start_char(char ch)
{
    return char_is_letter(ch) ||
        ch == ':' || ch == '_';
}

static inline bool
is_html_name_char(char ch)
{
    return is_html_name_start_char(ch) || char_is_digit(ch) ||
        ch == '-' || ch == '.';
}

#endif
