/*
 * Serialize objects portably into a buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SERIALIZE_HXX
#define BENG_PROXY_SERIALIZE_HXX

#include <stdint.h>

struct pool;
struct growing_buffer;
struct strmap;
template<typename T> struct ConstBuffer;

void
serialize_uint16(struct growing_buffer *gb, uint16_t value);

void
serialize_uint32(struct growing_buffer *gb, uint32_t value);

void
serialize_uint64(struct growing_buffer *gb, uint64_t value);

void
serialize_string(struct growing_buffer *gb, const char *value);

void
serialize_string_null(struct growing_buffer *gb, const char *value);

void
serialize_strmap(struct growing_buffer *gb, const struct strmap &map);

void
serialize_strmap(struct growing_buffer *gb, const struct strmap *map);

uint16_t
deserialize_uint16(ConstBuffer<void> &input);

uint32_t
deserialize_uint32(ConstBuffer<void> &input);

uint64_t
deserialize_uint64(ConstBuffer<void> &input);

const char *
deserialize_string(ConstBuffer<void> &input);

const char *
deserialize_string_null(ConstBuffer<void> &input);

struct strmap *
deserialize_strmap(ConstBuffer<void> &input, struct pool *pool);

#endif
