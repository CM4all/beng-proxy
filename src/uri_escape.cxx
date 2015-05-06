/*
 * Escape and unescape in URI style ('%20').
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri_escape.hxx"
#include "format.h"
#include "pool.hxx"
#include "util/CharUtil.hxx"

#include <string.h>

/**
 * @see RFC 3986 2.3
 */
constexpr
static inline bool
uri_harmless_char(char ch)
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
        if (uri_harmless_char(src[i])) {
            dest[dest_length++] = src[i];
        } else {
            dest[dest_length++] = escape_char;
            format_uint8_hex_fixed(&dest[dest_length], (uint8_t)src[i]);
            dest_length += 2;
        }
    }

    return dest_length;
}

const char *
uri_escape_dup(struct pool *pool, const char *src, size_t src_length,
               char escape_char)
{
    char *dest = (char *)p_malloc(pool, src_length * 3 + 1);
    size_t dest_length = uri_escape(dest, src, src_length, escape_char);
    dest[dest_length] = 0;
    return dest;
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

char *
uri_unescape_dup(struct pool *pool, const char *src, size_t length,
                 char escape_char)
{
    char *dest = (char *)p_malloc(pool, length + 1);
    memcpy(dest, src, length);
    size_t dest_length = uri_unescape_inplace(dest, length, escape_char);
    dest[dest_length] = 0;
    return dest;
}
