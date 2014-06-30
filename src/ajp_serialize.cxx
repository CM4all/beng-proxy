/*
 * Write an AJP stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp_serialize.hxx"
#include "serialize.hxx"
#include "growing_buffer.hxx"
#include "util/ConstBuffer.hxx"

#include <netinet/in.h>
#include <string.h>
#include <assert.h>

void
serialize_ajp_string_n(struct growing_buffer *gb, const char *s, size_t length)
{
    assert(gb != nullptr);
    assert(s != nullptr);

    if (length > 0xfffe)
        length = 0xfffe; /* XXX too long, cut off */

    void *v = growing_buffer_write(gb, 2 + length + 1);
    char *p = (char *)v;
    *(uint16_t *)v = htons(length);
    memcpy(p + 2, s, length);
    p[2 + length] = 0;
}

void
serialize_ajp_string(struct growing_buffer *gb, const char *s)
{
    if (s == nullptr) {
        /* 0xffff means nullptr; this is not documented, I have
           determined it from a wireshark dump */

        uint16_t *p = (uint16_t *)growing_buffer_write(gb, 2);
        *p = 0xffff;
        return;
    }

    serialize_ajp_string_n(gb, s, strlen(s));
}

void
serialize_ajp_integer(struct growing_buffer *gb, int i)
{
    serialize_uint16(gb, i);
}

void
serialize_ajp_bool(struct growing_buffer *gb, bool b)
{
    bool *p = (bool *)growing_buffer_write(gb, sizeof(*p));
    *p = b ? 1 : 0;
}

static void
SkipFront(ConstBuffer<void> &input, size_t n)
{
    assert(input.size >= n);

    input.data = (const uint8_t *)input.data + n;
    input.size -= n;
}

const char *
deserialize_ajp_string(ConstBuffer<void> &input)
{
    size_t length = deserialize_uint16(input);
    if (length == 0xffff)
        /* 0xffff means nullptr; this is not documented, I have
           determined it from a wireshark dump */
        return nullptr;

    const char *value = (const char *)input.data;

    if (input.size <= length || value[length] != 0) {
        input = nullptr;
        return nullptr;
    }

    SkipFront(input, length + 1);
    return value;
}
