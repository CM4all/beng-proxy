/*
 * Escape and unescape in URI style ('%20').
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_escape.hxx"
#include "format.h"
#include "util/CharUtil.hxx"
#include "util/StringView.hxx"

#include <algorithm>

#include <string.h>

/**
 * @see RFC 3986 2.3
 */
constexpr
static inline bool
IsUriUnreserved(char ch)
{
    return IsAlphaNumericASCII(ch) ||
        ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

size_t
uri_escape(char *dest, StringView src,
           char escape_char)
{
    size_t dest_length = 0;

    for (size_t i = 0; i < src.size; ++i) {
        if (IsUriUnreserved(src[i])) {
            dest[dest_length++] = src[i];
        } else {
            dest[dest_length++] = escape_char;
            format_uint8_hex_fixed(&dest[dest_length], (uint8_t)src[i]);
            dest_length += 2;
        }
    }

    return dest_length;
}

size_t
uri_escape(char *dest, ConstBuffer<void> src,
           char escape_char)
{
    return uri_escape(dest, StringView((const char *)src.data, src.size),
                      escape_char);
}

static int
parse_hexdigit(char ch)
{
    if (IsDigitASCII(ch))
        return ch - '0';
    else if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 0xa;
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 0xa;
    else
        return -1;
}

char *
uri_unescape(char *dest, StringView _src, char escape_char)
{
    auto src = _src.begin();
    const auto end = _src.end();

    while (true) {
        auto p = std::find(src, end, escape_char);
        dest = std::copy(src, p, dest);

        if (p == end)
            break;

        if (p >= end - 2)
            /* percent sign at the end of string */
            return nullptr;

        const int digit1 = parse_hexdigit(p[1]);
        const int digit2 = parse_hexdigit(p[2]);
        if (digit1 == -1 || digit2 == -1)
            /* invalid hex digits */
            return nullptr;

        const char ch = (char)((digit1 << 4) | digit2);
        if (ch == 0)
            /* no %00 hack allowed! */
            return nullptr;

        *dest++ = ch;
        src = p + 3;
    }

    return dest;
}
