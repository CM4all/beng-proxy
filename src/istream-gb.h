/*
 * A wrapper that turns a growing_buffer into an istream./
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_GB_H
#define BENG_PROXY_ISTREAM_GB_H

struct pool;
struct growing_buffer;

struct istream *
istream_gb_new(struct pool *pool, const struct growing_buffer *gb);

#endif
