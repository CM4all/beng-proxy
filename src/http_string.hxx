/*
 * HTTP string utilities according to RFC 2616 2.2.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_STRING_HXX
#define BENG_PROXY_HTTP_STRING_HXX

#include "util/StringView.hxx"

#include <inline/compiler.h>

struct pool;

static gcc_always_inline bool
char_is_http_char(char ch)
{
    return (ch & 0x80) == 0;
}

static gcc_always_inline bool
char_is_http_ctl(char ch)
{
    return (((unsigned char)ch) <= 0x1f) || ch == 0x7f;
}

static gcc_always_inline bool
char_is_http_text(char ch)
{
    return !char_is_http_ctl(ch);
}

static gcc_always_inline bool
char_is_http_sp(char ch)
{
    return ch == ' ';
}

static gcc_always_inline bool
char_is_http_ht(char ch)
{
    return ch == '\t';
}

gcc_pure
static gcc_always_inline bool
char_is_http_separator(char ch)
{
    return ch == '(' || ch == ')' || ch == '<' || ch == '>' ||
        ch == '@' || ch == ',' || ch == ';' || ch == ':' ||
        ch == '\\' || ch == '"' || ch == '/' ||
        ch == '[' || ch == ']' ||
        ch == '?' || ch == '=' || ch == '{' || ch == '}' ||
        char_is_http_sp(ch) || char_is_http_ht(ch);
}

gcc_pure
static gcc_always_inline bool
char_is_http_token(char ch)
{
    return char_is_http_char(ch) && !char_is_http_ctl(ch) &&
        !char_is_http_separator(ch);
}

void
http_next_token(StringView &input, StringView &value);

void
http_next_quoted_string(struct pool &pool, StringView &input,
                        StringView &value);

void
http_next_value(struct pool &pool, StringView &input, StringView &value);

void
http_next_name_value(struct pool &pool, StringView &input,
                     StringView &name, StringView &value);

gcc_pure
static inline bool
http_must_quote_token(StringView src)
{
    for (auto ch : src)
        if (!char_is_http_token(ch))
            return true;
    return false;
}

size_t
http_quote_string(char *dest, StringView src);

#endif
