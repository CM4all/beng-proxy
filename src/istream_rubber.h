/*
 * istream implementation which reads from a rubber allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_RUBBER_H
#define BENG_PROXY_ISTREAM_RUBBER_H

#include <stddef.h>

struct pool;
struct istream;
struct rubber;

struct istream *
istream_rubber_new(struct pool *pool, const struct rubber *rubber,
                   unsigned id, size_t start, size_t end);

#endif
