/*
 * URI character classification according to RFC 2396.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef URI_CHARS_HXX
#define URI_CHARS_HXX

#include "strutil.h"

static constexpr inline bool
char_is_uri_mark(char ch)
{
    return ch == '-' || ch == '_' || ch == '.' || ch == '!' || ch == '~' ||
        ch == '*' || ch == '\'' || ch == '(' || ch == ')';
}

/**
 * See RFC 2396 2.3.
 */
static inline bool
char_is_uri_unreserved(char ch)
{
    return char_is_alphanumeric(ch) || char_is_uri_mark(ch);
}

/**
 * See RFC 2396 3.3.
 */
static inline bool
char_is_uri_pchar(char ch)
{
    return char_is_uri_unreserved(ch) ||
        ch == '%' || /* "escaped" */
        ch == ':' || ch == '@' || ch == '&' || ch == '=' || ch == '+' ||
        ch == '$' || ch == ',';
}

#endif
