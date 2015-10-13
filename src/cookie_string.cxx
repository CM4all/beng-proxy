/*
 * Cookie string utilities according to RFC 6265 4.1.1.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cookie_string.hxx"
#include "http_string.hxx"
#include "pool.hxx"
#include "util/StringView.hxx"

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
cookie_next_unquoted_value(StringView &input, StringView &value)
{
    value.size = 0;
    value.data = input.data;

    while (value.size < input.size &&
           char_is_cookie_octet(input[value.size]))
        ++value.size;

    input.skip_front(value.size);
}

gcc_always_inline
static constexpr bool
char_is_rfc_ignorant_cookie_octet(char ch)
{
    return char_is_cookie_octet(ch) ||
        ch == ' ' || ch == ',';
}

static void
cookie_next_rfc_ignorant_value(StringView &input, StringView &value)
{
    value.size = 0;
    value.data = input.data;

    while (value.size < input.size &&
           char_is_rfc_ignorant_cookie_octet(input[value.size]))
        ++value.size;

    input.skip_front(value.size);
}

static void
cookie_next_value(struct pool &pool, StringView &input,
                  StringView &value)
{
    if (!input.IsEmpty() && input.front() == '"')
        http_next_quoted_string(pool, input, value);
    else
        cookie_next_unquoted_value(input, value);
}

static void
cookie_next_rfc_ignorant_value(struct pool &pool, StringView &input,
                               StringView &value)
{
    if (!input.IsEmpty() && input.front() == '"')
        http_next_quoted_string(pool, input, value);
    else
        cookie_next_rfc_ignorant_value(input, value);
}

void
cookie_next_name_value(struct pool &pool, StringView &input,
                       StringView &name, StringView &value,
                       bool rfc_ignorant)
{
    http_next_token(input, name);
    if (name.IsEmpty())
        return;

    input.StripLeft();
    if (!input.IsEmpty() && input.front() == '=') {
        input.pop_front();
        input.StripLeft();

        if (rfc_ignorant)
            cookie_next_rfc_ignorant_value(pool, input, value);
        else
            cookie_next_value(pool, input, value);
    } else
        value = nullptr;
}
