/*
 * A wrapper that turns a growing_buffer into an istream./
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_GB_HXX
#define BENG_PROXY_ISTREAM_GB_HXX

struct pool;
class Istream;
class GrowingBuffer;

Istream *
istream_gb_new(struct pool &pool, GrowingBuffer &&gb);

#endif
