/*
 * Serialize and deserialize FastCGI packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_SERIALIZE_H
#define BENG_PROXY_FCGI_SERIALIZE_H

#include <stdint.h>

struct growing_buffer;

/**
 * @param request_id the FastCGI request id in network byte order
 */
void
fcgi_serialize_params(struct growing_buffer *gb, uint16_t request_id,
                      const char *name, const char *value);

#endif
