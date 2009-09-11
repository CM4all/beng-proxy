/*
 * Serialize AJP request headers, deserialize response headers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_AJP_HEADERS_H
#define BENG_PROXY_AJP_HEADERS_H

#include "pool.h"

struct growing_buffer;
struct strmap;
struct strref;

/**
 * Serialize the specified headers to the buffer, but ignore "Content-Length".
 *
 * @return the number of headers which were written
 */
unsigned
serialize_ajp_headers(struct growing_buffer *gb, struct strmap *headers);

void
deserialize_ajp_headers(pool_t pool, struct strmap *headers,
                        struct strref *input, unsigned num_headers);

#endif
