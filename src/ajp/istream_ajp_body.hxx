/*
 * This istream filter wraps data inside AJPv13 packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_AJP_BODY_HXX
#define BENG_PROXY_ISTREAM_AJP_BODY_HXX

#include <stddef.h>

struct pool;
struct istream;

struct istream *
istream_ajp_body_new(struct pool *pool, struct istream *input);

void
istream_ajp_body_request(struct istream *istream, size_t length);

#endif
