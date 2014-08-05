/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_DEFLATE_HXX
#define BENG_PROXY_ISTREAM_DEFLATE_HXX

struct pool;
struct istream;

struct istream *
istream_deflate_new(struct pool *pool, struct istream *input);

#endif
