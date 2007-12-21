/*
 * Write HTTP headers into a fifo_buffer_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header-writer.h"

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
    *dest = '\n';
}

void
headers_copy(strmap_t in, growing_buffer_t out, const char *const* keys)
{
    const char *value;

    for (; *keys != NULL; ++keys) {
        value = strmap_get(in, *keys);
        if (value != NULL)
            header_write(out, *keys, value);
    }
}

void
headers_copy_all(strmap_t in, growing_buffer_t out)
{
    const struct strmap_pair *pair;

    strmap_rewind(in);

    while ((pair = strmap_next(in)) != NULL)
        header_write(out, pair->key, pair->value);
}
