/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_DPOOL_H
#define __BENG_DPOOL_H

#include <stddef.h>

struct dpool;
struct shm;

struct dpool *
dpool_new(struct shm *shm);

void
dpool_destroy(struct dpool *pool);

void *
d_malloc(struct dpool *pool, size_t size);

void
d_free(struct dpool *pool, const void *p);

#endif
