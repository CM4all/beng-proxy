/*
 * Write HTTP headers into a fifo_buffer_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header-writer.h"

#include <assert.h>
#include <string.h>

void
header_write(growing_buffer_t gb, const char *key, const char *value)
{
    size_t key_length, value_length;
    char *dest;

    key_length = strlen(key);
    value_length = strlen(value);

    dest = growing_buffer_write(gb, key_length + 2 + value_length + 2);

    memcpy(dest, key, key_length);
    dest += key_length;
    *dest++ = ':';
    *dest++ = ' ';
    memcpy(dest, value, value_length);
    dest += value_length;
    *dest++ = '\r';
    *dest++ = '\n';
}
