/*
 * A wrapper that turns a growing_buffer into an istream./
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_GB_HXX
#define BENG_PROXY_ISTREAM_GB_HXX

struct pool;
struct GrowingBuffer;

struct istream *
istream_gb_new(struct pool *pool, const GrowingBuffer *gb);

#endif
