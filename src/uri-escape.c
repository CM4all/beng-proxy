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
