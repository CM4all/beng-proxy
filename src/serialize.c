/*
 * Serialize objects portably into a buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "serialize.h"
#include "strmap.h"
#include "growing-buffer.h"
#include "strref.h"

#include <glib.h>

#include <stdint.h>
#include <string.h>

void
serialize_uint16(struct growing_buffer *gb, uint16_t value)
{
    uint16_t *dest;

    dest = growing_buffer_write(gb, sizeof(*dest));
    *dest = g_htons(value);
}

void
serialize_uint32(struct growing_buffer *gb, uint32_t value)
{
    uint32_t *dest;

    dest = growing_buffer_write(gb, sizeof(*dest));
    *dest = g_htonl(value);
}

void
serialize_uint64(struct growing_buffer *gb, uint64_t value)
{
    uint64_t *dest;

    dest = growing_buffer_write(gb, sizeof(*dest));
    *dest = GUINT64_TO_BE(value);
}

/*
static void
serialize_size_t(struct growing_buffer *gb, size_t value)
{
    serialize_uint32(gb, value);
}
*/

void
serialize_string(struct growing_buffer *gb, const char *value)
{
    assert(value != NULL);

    /* write the string including the null terminator */
    growing_buffer_write_buffer(gb, value, strlen(value) + 1);
}

void
serialize_string_null(struct growing_buffer *gb, const char *value)
{
    serialize_string(gb, value != NULL ? value : "");
}

void
serialize_strmap(struct growing_buffer *gb, struct strmap *map)
{
    const struct strmap_pair *pair;

    if (map == NULL) {
        /* same as empty map */
        serialize_string(gb, "");
        return;
    }

    strmap_rewind(map);

    while ((pair = strmap_next(map)) != NULL) {
        if (*pair->key == 0)
            /* this shouldn't happen; ignore this invalid entry  */
            continue;

        serialize_string(gb, pair->key);
        serialize_string(gb, pair->value);
    }

    /* key length 0 means "end of map" */
    serialize_string(gb, "");
}

uint16_t
deserialize_uint16(struct strref *input)
{
    uint16_t value;

    if (input->length < sizeof(value)) {
        strref_null(input);
        return 0;
    }

    value = g_ntohs(*(const uint16_t *)input->data);
    strref_skip(input, sizeof(value));

    return value;
}

uint32_t
deserialize_uint32(struct strref *input)
{
    uint32_t value;

    if (input->length < sizeof(value)) {
        strref_null(input);
        return 0;
    }

    value = g_ntohl(*(const uint32_t *)input->data);
    strref_skip(input, sizeof(value));

    return value;
}

uint64_t
deserialize_uint64(struct strref *input)
{
    uint64_t value;

    if (input->length < sizeof(value)) {
        strref_null(input);
        return 0;
    }

    value = GUINT64_FROM_BE(*(const uint64_t *)input->data);
    strref_skip(input, sizeof(value));

    return value;
}

const char *
deserialize_string(struct strref *input)
{
    const char *value, *end = strref_chr(input, 0);

    if (end == NULL) {
        strref_null(input);
        return NULL;
    }

    value = input->data;

    strref_skip(input, end + 1 - value);
    return value;
}

const char *
deserialize_string_null(struct strref *input)
{
    const char *value = deserialize_string(input);
    if (value != NULL && *value == 0)
        value = NULL;
    return value;
}

struct strmap *
deserialize_strmap(struct strref *input, pool_t pool)
{
    const char *key, *value;
    struct strmap *map;

    key = deserialize_string(input);
    if (key == NULL || *key == 0)
        return NULL;

    map = strmap_new(pool, 17);

    do {
        value = deserialize_string(input);
        if (value == NULL)
            return NULL;

        strmap_add(map, key, value);
        key = deserialize_string(input);
        if (key == NULL)
            return NULL;
    } while (*key != 0);

    return map;
}
