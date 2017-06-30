/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Response.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>

inline void
TrafoResponse::Grow(size_t new_capacity)
{
    assert(size <= capacity);
    assert(new_capacity > capacity);

    uint8_t *new_buffer = new uint8_t[new_capacity];
    std::copy_n(buffer, size, new_buffer);
    delete[] buffer;
    buffer = new_buffer;
    capacity = new_capacity;
}

void *
TrafoResponse::Write(size_t nbytes)
{
    assert(size <= capacity);

    const size_t new_size = size + nbytes;
    if (new_size > capacity)
        Grow(((new_size - 1) | 0x7fff) + 1);

    void *result = buffer + size;
    size = new_size;
    return result;
}

void
TrafoResponse::Packet(TranslationCommand cmd)
{
    const TranslationHeader header{0, cmd};
    void *p = Write(sizeof(header));
    memcpy(p, &header, sizeof(header));
}

void
TrafoResponse::Packet(TranslationCommand cmd, ConstBuffer<void> payload)
{
    assert(payload.size <= 0xffff);

    const TranslationHeader header{uint16_t(payload.size), cmd};
    void *p = Write(sizeof(header) + payload.size);
    p = mempcpy(p, &header, sizeof(header));
    memcpy(p, payload.data, payload.size);
}

void
TrafoResponse::Packet(TranslationCommand cmd, const char *payload)
{
    assert(payload != nullptr);

    Packet(cmd, payload, strlen(payload));
}
