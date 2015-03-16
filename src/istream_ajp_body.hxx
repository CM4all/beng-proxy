/*
 * This istream filter wraps data inside AJPv13 packets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_AJP_BODY_HXX
#define BENG_PROXY_ISTREAM_AJP_BODY_HXX

struct pool;
struct istream;

struct istream *
istream_ajp_body_new(struct pool *pool, struct istream *input);

#endif
