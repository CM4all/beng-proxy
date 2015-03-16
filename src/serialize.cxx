/*
 * Serialize objects portably into a buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "serialize.hxx"
#include "strmap.hxx"
#include "growing_buffer.hxx"
#include "util/ConstBuffer.hxx"
#include "util/ByteOrder.hxx"

#include <assert.h>
#include <stdint.h>
#include <string.h>

void
serialize_uint16(GrowingBuffer *gb, uint16_t value)
{
    uint16_t *dest = (uint16_t *)growing_buffer_write(gb, sizeof(*dest));
    *dest = ToBE16(value);
}

void
serialize_uint32(GrowingBuffer *gb, uint32_t value)
{
    uint32_t *dest = (uint32_t *)growing_buffer_write(gb, sizeof(*dest));
    *dest = ToBE32(value);
}

void
serialize_uint64(GrowingBuffer *gb, uint64_t value)
{
    uint64_t *dest = (uint64_t *)growing_buffer_write(gb, sizeof(*dest));
    *dest = ToBE64(value);
}

/*
static void
serialize_size_t(GrowingBuffer *gb, size_t value)
{
    serialize_uint32(gb, value);
}
*/

void
serialize_string(GrowingBuffer *gb, const char *value)
{
    assert(value != nullptr);

    /* write the string including the null terminator */
    growing_buffer_write_buffer(gb, value, strlen(value) + 1);
}

void
serialize_string_null(GrowingBuffer *gb, const char *value)
{
    serialize_string(gb, value != nullptr ? value : "");
}

void
serialize_strmap(GrowingBuffer *gb, const struct strmap &map)
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
serialize_strmap(GrowingBuffer *gb, const struct strmap *map)
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

uint16_t
deserialize_uint16(ConstBuffer<void> &input)
{
    uint16_t value;

    if (input.size < sizeof(value)) {
        input = nullptr;
        return 0;
    }

    value = FromBE16(*(const uint16_t *)input.data);
    SkipFront(input, sizeof(value));

    return value;
}

uint32_t
deserialize_uint32(ConstBuffer<void> &input)
{
    uint32_t value;

    if (input.size < sizeof(value)) {
        input = nullptr;
        return 0;
    }

    value = FromBE32(*(const uint32_t *)input.data);
    SkipFront(input, sizeof(value));

    return value;
}

uint64_t
deserialize_uint64(ConstBuffer<void> &input)
{
    uint64_t value;

    if (input.size < sizeof(value)) {
        input = nullptr;
        return 0;
    }

    value = FromBE64(*(const uint64_t *)input.data);
    SkipFront(input, sizeof(value));

    return value;
}

const char *
deserialize_string(ConstBuffer<void> &input)
{
    const char *end = (const char *)memchr(input.data, 0, input.size);
    if (end == nullptr) {
        input = nullptr;
        return nullptr;
    }

    const char *value = (const char *)input.data;

    SkipFront(input, end + 1 - value);
    return value;
}

const char *
deserialize_string_null(ConstBuffer<void> &input)
{
    const char *value = deserialize_string(input);
    if (value != nullptr && *value == 0)
        value = nullptr;
    return value;
}

bool
deserialize_strmap(ConstBuffer<void> &input, struct strmap &dest)
{
    while (true) {
        const char *key = deserialize_string(input);
        if (key == nullptr)
            return false;

        if (*key == 0)
            return true;

        const char *value = deserialize_string(input);
        if (value == nullptr)
            return false;

        dest.Add(key, value);
    }
}

struct strmap *
deserialize_strmap(ConstBuffer<void> &input, struct pool *pool)
{
    const char *key, *value;
    struct strmap *map;

    key = deserialize_string(input);
    if (key == nullptr || *key == 0)
        return nullptr;

    map = strmap_new(pool);

    do {
        value = deserialize_string(input);
        if (value == nullptr)
            return nullptr;

        map->Add(key, value);
        key = deserialize_string(input);
        if (key == nullptr)
            return nullptr;
    } while (*key != 0);

    return map;
}
