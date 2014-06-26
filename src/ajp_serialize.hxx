/*
 * Serialize and deserialize AJP packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_AJP_SERIALIZE_HXX
#define BENG_PROXY_AJP_SERIALIZE_HXX

#include <stddef.h>

struct growing_buffer;
template<typename T> struct ConstBuffer;

void
serialize_ajp_string_n(struct growing_buffer *gb, const char *s, size_t length);

void
serialize_ajp_string(struct growing_buffer *gb, const char *s);

void
serialize_ajp_integer(struct growing_buffer *gb, int i);

void
serialize_ajp_bool(struct growing_buffer *gb, bool b);

const char *
deserialize_ajp_string(ConstBuffer<void> &input);

#endif
