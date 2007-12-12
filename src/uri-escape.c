/*
 * Escape and unescape in URI style ('%20').
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "uri.h"
#include "strutil.h"
#include "format.h"

#include <string.h>

static inline int
uri_harmless_char(char ch)
{
    return char_is_alphanumeric(ch) ||
        ch == '_' || ch == '-' || ch == '/' || ch == '.';
}

size_t
uri_escape(char *dest, const char *src, size_t src_length)
{
    size_t i, dest_length = 0;

    for (i = 0; i < src_length; ++i) {
        if (uri_harmless_char(src[i])) {
            dest[dest_length++] = src[i];
        } else {
            dest[dest_length++] = '%';
            format_uint8_hex_fixed(&dest[dest_length], (uint8_t)src[i]);
            dest_length += 2;
        }
    }

    return dest_length;
}

static int
parse_hexdigit(char ch)
{
    if (char_is_digit(ch))
        return ch - '0';
    else if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 0x10;
    else if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 0x10;
    else
        return -1;
}

size_t
uri_unescape_inplace(char *src, size_t length)
{
    char *end = src + length, *current = src, *p;
    int digit1, digit2;
    char ch;

    while ((p = memchr(current, '%', end - current)) != NULL) {
        if (p >= end - 2)
            /* percent sign at the end of string */
            return 0;

        digit1 = parse_hexdigit(p[1]);
        digit2 = parse_hexdigit(p[2]);
        if (digit1 == -1 || digit2 == -1)
            /* invalid hex digits */
            return 0;

        ch = (char)((digit1 << 4) | digit2);
        if (ch == 0)
            /* no %00 hack allowed! */
            return 0;

        *p = ch;
        memmove(p + 1, p + 3, end - p - 3);
        end -= 2;
    }

    return end - src;
}
