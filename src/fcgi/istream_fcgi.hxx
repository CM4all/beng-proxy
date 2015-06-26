/*
 * Convert a stream into a stream of FCGI_STDIN packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_FCGI_HXX
#define BENG_PROXY_ISTREAM_FCGI_HXX

#include <stdint.h>

struct pool;
struct istream;

/**
 * @param request_id the FastCGI request id in network byte order
 */
struct istream *
istream_fcgi_new(struct pool *pool, struct istream *input, uint16_t request_id);

#endif
