/*
 * Write HTTP headers into a fifo_buffer_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header-writer.h"
#include "strmap.h"
#include "growing-buffer.h"

#include <string.h>

void
header_write(struct growing_buffer *gb, const char *key, const char *value)
{
    size_t key_length, value_length;
    char *dest;

    assert(gb != NULL);
    assert(key != NULL);
    assert(value != NULL);

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
headers_copy(struct strmap *in, struct growing_buffer *out, const char *const* keys)
{
    const char *value;

    for (; *keys != NULL; ++keys) {
        value = strmap_get(in, *keys);
        if (value != NULL)
            header_write(out, *keys, value);
    }
}

void
headers_copy_all(struct strmap *in, struct growing_buffer *out)
{
    const struct strmap_pair *pair;

    assert(in != NULL);
    assert(out != NULL);

    strmap_rewind(in);

    while ((pair = strmap_next(in)) != NULL)
        header_write(out, pair->key, pair->value);
}

struct growing_buffer *
headers_dup(pool_t pool, struct strmap *in)
{
    struct growing_buffer *out = growing_buffer_new(pool, 2048);
    headers_copy_all(in, out);
    return out;
}
