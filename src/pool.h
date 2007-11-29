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

#ifdef NDEBUG

#define pool_set_major(pool)

#else

void
pool_set_major(pool_t pool);

#endif

#ifdef DEBUG_POOL_REF

void
pool_ref_debug(pool_t pool, const char *file, unsigned line);

unsigned
pool_unref_debug(pool_t pool, const char *file, unsigned line);

#define pool_ref(pool) pool_ref_debug(pool, __FILE__, __LINE__)

#define pool_unref(pool) pool_unref_debug(pool, __FILE__, __LINE__)

#else

void
pool_ref(pool_t pool);

unsigned
pool_unref(pool_t pool);

#endif

#ifdef NDEBUG
static inline void
pool_commit(void)
{
}
#else
void
pool_commit(void);

int
pool_contains(pool_t pool, const void *ptr, size_t size);
#endif

void * attr_malloc
p_malloc(pool_t pool, size_t size);

void
p_free(pool_t pool, void *ptr);

void * attr_malloc
p_calloc(pool_t pool, size_t size);

char * attr_malloc
p_memdup(pool_t pool, const void *src, size_t length);

char * attr_malloc
p_strdup(pool_t pool, const char *src);

char * attr_malloc
p_strndup(pool_t pool, const char *src, size_t length);

char * attr_malloc attr_printf(2, 3)
p_sprintf(pool_t pool, const char *fmt, ...);

char * attr_malloc
p_strcat(pool_t pool, const char *s, ...);

char * attr_malloc
p_strncat(pool_t pool, const char *s, size_t length, ...);

#endif
