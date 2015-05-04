/*
 * Classify characters in a HTML/XML document.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef HTML_CHARS_HXX
#define HTML_CHARS_HXX

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
