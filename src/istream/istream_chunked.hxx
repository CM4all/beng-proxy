/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_CHUNKED_HXX
#define BENG_PROXY_ISTREAM_CHUNKED_HXX

struct pool;
class Istream;

Istream *
istream_chunked_new(struct pool &pool, Istream &input);

#endif
