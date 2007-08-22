/*
 * Memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_POOL_H
#define __BENG_POOL_H

#include "compiler.h"

#include <stddef.h>

typedef struct pool *pool_t;

void
pool_recycler_clear(void);

pool_t
pool_new_libc(pool_t parent, const char *name);

pool_t
pool_new_linear(pool_t parent, const char *name, size_t initial_size);

void
pool_ref(pool_t pool);

unsigned
pool_unref(pool_t pool);

#ifdef NDEBUG
static inline void
pool_commit(void)
{
}
#else
void
pool_commit(void);
#endif

void * attr_malloc
p_malloc(pool_t pool, size_t size);

void * attr_malloc
p_calloc(pool_t pool, size_t size);

char * attr_malloc
p_strdup(pool_t pool, const char *src);

char * attr_malloc
p_strndup(pool_t pool, const char *src, size_t length);

char * attr_malloc attr_printf(2, 3)
p_sprintf(pool_t pool, const char *fmt, ...);

#endif
