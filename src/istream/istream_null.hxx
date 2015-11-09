/*
 * istream implementation which reads nothing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_NULL_HXX
#define BENG_PROXY_ISTREAM_NULL_HXX

struct pool;
class Istream;

Istream *
istream_null_new(struct pool *pool);

#endif
