/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_ZERO_HXX
#define BENG_PROXY_ISTREAM_ZERO_HXX

struct pool;
struct istream;

/**
 * istream implementation which reads zero bytes.
 */
struct istream *
istream_zero_new(struct pool *pool);

#endif
