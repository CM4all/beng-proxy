/*
 * HTTP string utilities according to RFC 2616 2.2.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_STRING_H
#define __BENG_HTTP_STRING_H

#include "strref.h"

#include <inline/compiler.h>

static __attr_always_inline int
char_is_http_char(char ch)
{
    return (ch & 0x80) == 0;
}

static __attr_always_inline int
char_is_http_ctl(char ch)
{
    return (ch >= 0 && ch <= 0x1f) || ch == 0x7f;
}

static __attr_always_inline int
char_is_http_text(char ch)
{
    return !char_is_http_ctl(ch);
}

static __attr_always_inline int
char_is_http_sp(char ch)
{
    return ch == ' ';
}

static __attr_always_inline int
char_is_http_ht(char ch)
{
    return ch == '\t';
}

static __attr_always_inline int
char_is_http_separator(char ch)
{
    return ch == '(' || ch == ')' || ch == '<' || ch == '>' ||
        ch == '@' || ch == ',' || ch == ';' || ch == ':' ||
        ch == '\\' || ch == '"' || ch == '/' ||
        ch == '[' || ch == ']' ||
        ch == '?' || ch == '=' || ch == '{' || ch == '}' ||
        char_is_http_sp(ch) || char_is_http_ht(ch);
}

static __attr_always_inline int
char_is_http_token(char ch)
{
    return char_is_http_char(ch) && !char_is_http_ctl(ch) &&
        !char_is_http_separator(ch);
}

void
http_next_token(struct strref *input, struct strref *value);

void
http_next_quoted_string(pool_t pool, struct strref *input, struct strref *value);

void
http_next_value(pool_t pool, struct strref *input, struct strref *value);

size_t
http_quote_string(char *dest, struct strref *src);

#endif
