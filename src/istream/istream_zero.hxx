/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_ZERO_HXX
#define BENG_PROXY_ISTREAM_ZERO_HXX

struct pool;
class Istream;

/**
 * istream implementation which reads zero bytes.
 */
Istream *
istream_zero_new(struct pool *pool);

#endif
