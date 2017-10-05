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

#include "serialize.hxx"
#include "strmap.hxx"
#include "GrowingBuffer.hxx"
#include "util/ConstBuffer.hxx"
#include "util/ByteOrder.hxx"

#include <assert.h>
#include <stdint.h>
#include <string.h>

void
serialize_uint16(GrowingBuffer &gb, uint16_t value)
{
    uint16_t *dest = (uint16_t *)gb.Write(sizeof(*dest));
    *dest = ToBE16(value);
}

void
serialize_uint32(GrowingBuffer &gb, uint32_t value)
{
    uint32_t *dest = (uint32_t *)gb.Write(sizeof(*dest));
    *dest = ToBE32(value);
}

void
serialize_uint64(GrowingBuffer &gb, uint64_t value)
{
    uint64_t *dest = (uint64_t *)gb.Write(sizeof(*dest));
    *dest = ToBE64(value);
}

/*
static void
serialize_size_t(GrowingBuffer &gb, size_t value)
{
    serialize_uint32(gb, value);
}
*/

void
serialize_string(GrowingBuffer &gb, const char *value)
{
    assert(value != nullptr);

    /* write the string including the null terminator */
    gb.Write(value, strlen(value) + 1);
}

void
serialize_string_null(GrowingBuffer &gb, const char *value)
{
    serialize_string(gb, value != nullptr ? value : "");
}

void
serialize_strmap(GrowingBuffer &gb, const StringMap &map)
{
    for (const auto &i : map) {
        if (*i.key == 0)
            /* this shouldn't happen; ignore this invalid entry  */
            continue;

        serialize_string(gb, i.key);
        serialize_string(gb, i.value);
    }

    /* key length 0 means "end of map" */
    serialize_string(gb, "");
}

void
serialize_strmap(GrowingBuffer &gb, const StringMap *map)
{
    if (map == nullptr)
        /* same as empty map */
        serialize_string(gb, "");
    else
        serialize_strmap(gb, *map);
}

static void
SkipFront(ConstBuffer<void> &input, size_t n)
{
    assert(input.size >= n);

    input.data = (const uint8_t *)input.data + n;
    input.size -= n;
}

template<typename T>
static void
DeserializeT(ConstBuffer<void> &input, T &dest)
{
    static_assert(std::is_trivial<T>::value, "type is not trivial");

    if (gcc_unlikely(input.size < sizeof(dest)))
        throw DeserializeError();

    memcpy(&dest, input.data, sizeof(dest));
    SkipFront(input, sizeof(dest));
}

uint16_t
deserialize_uint16(ConstBuffer<void> &input)
{
    uint16_t value;
    DeserializeT(input, value);
    return FromBE16(*(const uint16_t *)input.data);
}

uint32_t
deserialize_uint32(ConstBuffer<void> &input)
{
    uint32_t value;
    DeserializeT(input, value);
    return FromBE32(*(const uint32_t *)input.data);
}

uint64_t
deserialize_uint64(ConstBuffer<void> &input)
{
    uint64_t value;
    DeserializeT(input, value);
    return FromBE64(*(const uint64_t *)input.data);
}

const char *
deserialize_string(ConstBuffer<void> &input)
{
    const char *end = (const char *)memchr(input.data, 0, input.size);
    if (end == nullptr)
        throw DeserializeError();

    const char *value = (const char *)input.data;

    SkipFront(input, end + 1 - value);
    return value;
}

const char *
deserialize_string_null(ConstBuffer<void> &input)
{
    const char *value = deserialize_string(input);
    if (*value == 0)
        value = nullptr;
    return value;
}

void
deserialize_strmap(ConstBuffer<void> &input, StringMap &dest)
{
    while (true) {
        const char *key = deserialize_string(input);
        if (*key == 0)
            break;

        const char *value = deserialize_string(input);

        dest.Add(key, value);
    }
}

StringMap *
deserialize_strmap(ConstBuffer<void> &input, struct pool &pool)
{
    const char *key, *value;

    key = deserialize_string(input);
    if (*key == 0)
        return nullptr;

    auto *map = strmap_new(&pool);

    do {
        value = deserialize_string(input);
        map->Add(key, value);
        key = deserialize_string(input);
    } while (*key != 0);

    return map;
}
