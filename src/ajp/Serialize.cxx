/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
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

#include "Serialize.hxx"
#include "../serialize.hxx"
#include "GrowingBuffer.hxx"
#include "util/ConstBuffer.hxx"
#include "util/ByteOrder.hxx"

#include <string.h>
#include <assert.h>

void
serialize_ajp_string_n(GrowingBuffer &gb, const char *s, size_t length)
{
    assert(s != nullptr);

    if (length > 0xfffe)
        length = 0xfffe; /* XXX too long, cut off */

    void *v = gb.Write(2 + length + 1);
    char *p = (char *)v;
    *(uint16_t *)v = ToBE16(length);
    memcpy(p + 2, s, length);
    p[2 + length] = 0;
}

void
serialize_ajp_string(GrowingBuffer &gb, const char *s)
{
    if (s == nullptr) {
        /* 0xffff means nullptr; this is not documented, I have
           determined it from a wireshark dump */

        auto *p = (uint16_t *)gb.Write(2);
        *p = 0xffff;
        return;
    }

    serialize_ajp_string_n(gb, s, strlen(s));
}

void
serialize_ajp_integer(GrowingBuffer &gb, int i)
{
    serialize_uint16(gb, i);
}

void
serialize_ajp_bool(GrowingBuffer &gb, bool b)
{
    bool *p = (bool *)gb.Write(sizeof(*p));
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

    if (input.size <= length || value[length] != 0)
        throw DeserializeError();

    SkipFront(input, length + 1);
    return value;
}
