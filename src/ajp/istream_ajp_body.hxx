/*
 * This istream filter wraps data inside AJPv13 packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_AJP_BODY_HXX
#define BENG_PROXY_ISTREAM_AJP_BODY_HXX

#include <stddef.h>

struct pool;
class Istream;

Istream *
istream_ajp_body_new(struct pool &pool, Istream &input);

void
istream_ajp_body_request(Istream &istream, size_t length);

#endif
