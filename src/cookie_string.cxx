/*
 * Cookie string utilities according to RFC 6265 4.1.1.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie_string.hxx"
#include "http_string.hxx"
#include "strref.h"
#include "strref2.h"
#include "pool.h"

gcc_always_inline
static constexpr bool
char_is_cookie_octet(char ch)
{
    return ch == 0x21 || (ch >= 0x23 && ch <= 0x2b) ||
        (ch >= 0x2d && ch <= 0x3a) ||
        (ch >= 0x3c && ch <= 0x5b) ||
        (ch >= 0x5d && ch <= 0x7e);
}

static void
cookie_next_unquoted_value(struct strref *input, struct strref *value)
{
    value->length = 0;
    value->data = input->data;

    while (value->length < input->length &&
           char_is_cookie_octet(input->data[value->length]))
        ++value->length;

    if (value->length > 0)
        strref_skip(input, value->length);
}

gcc_always_inline
static constexpr bool
char_is_rfc_ignorant_cookie_octet(char ch)
{
    return char_is_cookie_octet(ch) ||
        ch == ' ' || ch == ',';
}

static void
cookie_next_rfc_ignorant_value(struct strref *input, struct strref *value)
{
    value->length = 0;
    value->data = input->data;

    while (value->length < input->length &&
           char_is_rfc_ignorant_cookie_octet(input->data[value->length]))
        ++value->length;

    if (value->length > 0)
        strref_skip(input, value->length);
}

static void
cookie_next_value(struct pool *pool, struct strref *input,
                  struct strref *value)
{
    if (!strref_is_empty(input) && input->data[0] == '"')
        http_next_quoted_string(pool, input, value);
    else
        cookie_next_unquoted_value(input, value);
}

static void
cookie_next_rfc_ignorant_value(struct pool *pool, struct strref *input,
                               struct strref *value)
{
    if (!strref_is_empty(input) && input->data[0] == '"')
        http_next_quoted_string(pool, input, value);
    else
        cookie_next_rfc_ignorant_value(input, value);
}

void
cookie_next_name_value(struct pool *pool, struct strref *input,
                       struct strref *name, struct strref *value,
                       bool rfc_ignorant)
{
    http_next_token(input, name);
    if (strref_is_empty(name))
        return;

    strref_ltrim(input);
    if (!strref_is_empty(input) && input->data[0] == '=') {
        strref_skip(input, 1);
        strref_ltrim(input);

        if (rfc_ignorant)
            cookie_next_rfc_ignorant_value(pool, input, value);
        else
            cookie_next_value(pool, input, value);
    } else
        strref_clear(value);
}
