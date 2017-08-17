/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
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
