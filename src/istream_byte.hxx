/*
 * This istream filter passes one byte at a time.  This is useful for
 * testing and debugging istream handler implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_BYTE_HXX
#define BENG_PROXY_ISTREAM_BYTE_HXX

struct pool;
struct istream;

struct istream *
istream_byte_new(struct pool *pool, struct istream *input);

#endif
