/*
 * istream implementation which reads from a rubber allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_RUBBER_H
#define BENG_PROXY_ISTREAM_RUBBER_H

#include <stdbool.h>
#include <stddef.h>

struct pool;
struct istream;
struct rubber;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @param auto_remove shall the allocation be removed when this
 * istream is closed?
 */
struct istream *
istream_rubber_new(struct pool *pool, struct rubber *rubber,
                   unsigned id, size_t start, size_t end,
                   bool auto_remove);

#ifdef __cplusplus
}
#endif

#endif
