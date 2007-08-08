/*
 * Memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_POOL_H
#define __BENG_POOL_H

#include <sys/types.h>

typedef struct pool *pool_t;

pool_t
pool_new_libc(pool_t parent, const char *name);

pool_t
pool_new_linear(pool_t parent, const char *name, size_t initial_size);

void
pool_destroy(pool_t pool);

void *
p_malloc(pool_t pool, size_t size);

void *
p_calloc(pool_t pool, size_t size);

#endif
