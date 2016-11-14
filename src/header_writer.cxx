/*
 * Write HTTP headers into a fifo_buffer_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "header_writer.hxx"
#include "strmap.hxx"
#include "GrowingBuffer.hxx"

#include <http/header.h>

#include <assert.h>
#include <string.h>

void
header_write_begin(GrowingBuffer &buffer, const char *name)
{
    assert(name != nullptr);
    assert(*name != 0);

    size_t name_length = strlen(name);
    char *dest = (char *)buffer.Write(name_length + 2);

    memcpy(dest, name, name_length);
    dest += name_length;
    *dest++ = ':';
    *dest++ = ' ';
}

void
header_write_finish(GrowingBuffer &buffer)
{
    buffer.Write("\r\n", 2);
}

void
header_write(GrowingBuffer &buffer, const char *key, const char *value)
{
    size_t key_length, value_length;

    assert(key != nullptr);
    assert(value != nullptr);

    key_length = strlen(key);
    value_length = strlen(value);

    char *dest = (char *)buffer.Write(key_length + 2 + value_length + 2);

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
headers_copy_one(const StringMap &in, GrowingBuffer &out,
                 const char *key)
{
    const char *value = in.Get(key);
    if (value != nullptr)
        header_write(out, key, value);
}

void
headers_copy(const StringMap &in, GrowingBuffer &out,
             const char *const* keys)
{
    for (; *keys != nullptr; ++keys) {
        const char *value = in.Get(*keys);
        if (value != nullptr)
            header_write(out, *keys, value);
    }
}

void
headers_copy_all(const StringMap &in, GrowingBuffer &out)
{
    for (const auto &i : in)
        header_write(out, i.key, i.value);
}

void
headers_copy_most(const StringMap &in, GrowingBuffer &out)
{
    for (const auto &i : in)
        if (!http_header_is_hop_by_hop(i.key))
            header_write(out, i.key, i.value);
}

GrowingBuffer
headers_dup(struct pool &pool, const StringMap &in)
{
    GrowingBuffer out(pool, 2048);
    headers_copy_most(in, out);
    return out;
}
