/*
 * Write HTTP headers into a fifo_buffer_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header_writer.hxx"
#include "strmap.hxx"
#include "growing_buffer.hxx"

#include <http/header.h>

#include <assert.h>
#include <string.h>

void
header_write_begin(struct growing_buffer *gb, const char *name)
{
    assert(gb != nullptr);
    assert(name != nullptr);
    assert(*name != 0);

    size_t name_length = strlen(name);
    char *dest = (char *)growing_buffer_write(gb, name_length + 2);

    memcpy(dest, name, name_length);
    dest += name_length;
    *dest++ = ':';
    *dest++ = ' ';
}

void
header_write_finish(struct growing_buffer *gb)
{
    assert(gb != nullptr);

    growing_buffer_write_buffer(gb, "\r\n", 2);
}

void
header_write(struct growing_buffer *gb, const char *key, const char *value)
{
    size_t key_length, value_length;

    assert(gb != nullptr);
    assert(key != nullptr);
    assert(value != nullptr);

    key_length = strlen(key);
    value_length = strlen(value);

    char *dest = (char *)
        growing_buffer_write(gb, key_length + 2 + value_length + 2);

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
headers_copy_one(const struct strmap *in, struct growing_buffer *out,
                 const char *key)
{
    assert(in != nullptr);
    assert(out != nullptr);

    const char *value = in->Get(key);
    if (value != nullptr)
        header_write(out, key, value);
}

void
headers_copy(const struct strmap *in, struct growing_buffer *out,
             const char *const* keys)
{
    const char *value;

    for (; *keys != nullptr; ++keys) {
        value = in->Get(*keys);
        if (value != nullptr)
            header_write(out, *keys, value);
    }
}

void
headers_copy_all(const struct strmap *in, struct growing_buffer *out)
{
    assert(in != nullptr);
    assert(out != nullptr);

    for (const auto &i : *in)
        header_write(out, i.key, i.value);
}

void
headers_copy_most(const struct strmap *in, struct growing_buffer *out)
{
    assert(in != nullptr);
    assert(out != nullptr);

    for (const auto &i : *in)
        if (!http_header_is_hop_by_hop(i.key))
            header_write(out, i.key, i.value);
}

struct growing_buffer *
headers_dup(struct pool *pool, const struct strmap *in)
{
    struct growing_buffer *out = growing_buffer_new(pool, 2048);
    headers_copy_most(in, out);
    return out;
}
