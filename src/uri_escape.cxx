/*
 * Escape and unescape in URI style ('%20').
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_escape.hxx"
#include "format.h"
#include "util/CharUtil.hxx"

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
uri_escape(char *dest, const char *src, size_t src_length,
           char escape_char)
{
    size_t i, dest_length = 0;

    for (i = 0; i < src_length; ++i) {
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
uri_unescape(char *dest, const char *src, size_t length, char escape_char)
{
    const auto end = src + length;

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

size_t
uri_unescape_inplace(char *src, size_t length, char escape_char)
{
    char *end = src + length, *p = src;

    while ((p = (char *)memchr(p, escape_char, end - p)) != nullptr) {
        if (p >= end - 2)
            /* percent sign at the end of string */
            return 0;

        const int digit1 = parse_hexdigit(p[1]);
        const int digit2 = parse_hexdigit(p[2]);
        if (digit1 == -1 || digit2 == -1)
            /* invalid hex digits */
            return 0;

        const char ch = (char)((digit1 << 4) | digit2);
        if (ch == 0)
            /* no %00 hack allowed! */
            return 0;

        *p++ = ch;
        memmove(p, p + 2, end - p - 2);
        end -= 2;
    }

    return end - src;
}
