/*
 * Serialize and deserialize AJP packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_AJP_SERIALIZE_H
#define BENG_PROXY_AJP_SERIALIZE_H

#include <stdbool.h>

struct growing_buffer;
struct strref;

void
serialize_ajp_string(struct growing_buffer *gb, const char *s);

void
serialize_ajp_integer(struct growing_buffer *gb, int i);

void
serialize_ajp_bool(struct growing_buffer *gb, bool b);

const char *
deserialize_ajp_string(struct strref *input);

#endif
