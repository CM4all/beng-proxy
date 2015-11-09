/*
 * istream implementation which blocks indefinitely until closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_BLOCK_HXX
#define BENG_PROXY_ISTREAM_BLOCK_HXX

struct pool;
class Istream;

Istream *
istream_block_new(struct pool &pool);

#endif
