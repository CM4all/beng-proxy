/*
 * Serialize objects portably into a buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SERIALIZE_H
#define BENG_PROXY_SERIALIZE_H

#include "pool.h"

#include <stdint.h>

struct growing_buffer;
struct strmap;
struct strref;

void
serialize_uint16(struct growing_buffer *gb, uint16_t value);

void
serialize_uint32(struct growing_buffer *gb, uint32_t value);

void
serialize_string(struct growing_buffer *gb, const char *value);

void
serialize_strmap(struct growing_buffer *gb, struct strmap *map);

uint16_t
deserialize_uint16(struct strref *input);

uint32_t
deserialize_uint32(struct strref *input);

const char *
deserialize_string(struct strref *input);

struct strmap *
deserialize_strmap(struct strref *input, pool_t pool);

#endif
