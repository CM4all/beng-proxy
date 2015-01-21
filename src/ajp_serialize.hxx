/*
 * Serialize and deserialize AJP packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_AJP_SERIALIZE_HXX
#define BENG_PROXY_AJP_SERIALIZE_HXX

#include <stddef.h>

struct GrowingBuffer;
template<typename T> struct ConstBuffer;

void
serialize_ajp_string_n(GrowingBuffer *gb, const char *s, size_t length);

void
serialize_ajp_string(GrowingBuffer *gb, const char *s);

void
serialize_ajp_integer(GrowingBuffer *gb, int i);

void
serialize_ajp_bool(GrowingBuffer *gb, bool b);

const char *
deserialize_ajp_string(ConstBuffer<void> &input);

#endif
