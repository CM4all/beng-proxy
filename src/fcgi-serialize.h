/*
 * Serialize and deserialize FastCGI packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_SERIALIZE_H
#define BENG_PROXY_FCGI_SERIALIZE_H

struct growing_buffer;

void
fcgi_serialize_params(struct growing_buffer *gb, const char *name,
                      const char *value);

#endif
