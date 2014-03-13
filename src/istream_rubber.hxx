/*
 * istream implementation which reads from a rubber allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_RUBBER_HXX
#define BENG_PROXY_ISTREAM_RUBBER_HXX

#include <stddef.h>

struct pool;
struct istream;
class Rubber;

/**
 * @param auto_remove shall the allocation be removed when this
 * istream is closed?
 */
struct istream *
istream_rubber_new(struct pool *pool, Rubber *rubber,
                   unsigned id, size_t start, size_t end,
                   bool auto_remove);

#endif
