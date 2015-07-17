/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_DEFLATE_HXX
#define BENG_PROXY_ISTREAM_DEFLATE_HXX

struct pool;
struct istream;

/**
 * @param gzip use the gzip format instead of the zlib format?
 */
struct istream *
istream_deflate_new(struct pool *pool, struct istream *input,
                    bool gzip=false);

#endif
