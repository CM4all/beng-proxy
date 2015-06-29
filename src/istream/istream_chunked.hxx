/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_CHUNKED_HXX
#define BENG_PROXY_ISTREAM_CHUNKED_HXX

struct pool;
struct istream;

struct istream *
istream_chunked_new(struct pool *pool, struct istream *input);

#endif
