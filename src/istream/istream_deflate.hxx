/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_DEFLATE_HXX
#define BENG_PROXY_ISTREAM_DEFLATE_HXX

struct pool;
class Istream;

/**
 * @param gzip use the gzip format instead of the zlib format?
 */
Istream *
istream_deflate_new(struct pool *pool, Istream &input, bool gzip=false);

#endif
