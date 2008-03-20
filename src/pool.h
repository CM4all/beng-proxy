/*
 * Memory pool.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_POOL_H
#define __BENG_POOL_H

#include "trace.h"

#include <inline/compiler.h>

#include <stddef.h>

typedef struct pool *pool_t;

struct pool_mark {
    struct linear_pool_area *area;
    size_t position;
};

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

void
pool_ref_impl(pool_t pool TRACE_ARGS_DECL);

#define pool_ref(pool) pool_ref_impl(pool TRACE_ARGS)
#define pool_ref_fwd(pool) pool_ref_impl(pool TRACE_ARGS_FWD)

unsigned
pool_unref_impl(pool_t pool TRACE_ARGS_DECL);

#define pool_unref(pool) pool_unref_impl(pool TRACE_ARGS)
#define pool_unref_fwd(pool) pool_unref_impl(pool TRACE_ARGS_FWD)

#ifndef NDEBUG
#include <inline/list.h>

struct pool_notify {
    struct list_head siblings;
    int destroyed;
};

void
pool_notify(pool_t pool, struct pool_notify *notify);

static inline int
pool_denotify(struct pool_notify *notify)
{
    if (notify->destroyed)
        return 1;
    list_remove(&notify->siblings);
    return 0;
}
#endif

#ifdef NDEBUG
#else
#endif

#ifdef NDEBUG

static inline void
pool_trash(pool_t pool __attr_unused)
{
}

static inline void
pool_commit(void)
{
}

#else

void
pool_trash(pool_t pool __attr_unused);

void
pool_commit(void);

int
pool_contains(pool_t pool, const void *ptr, size_t size);

#endif

void
pool_mark(pool_t pool, struct pool_mark *mark);

void
pool_rewind(pool_t pool, const struct pool_mark *mark);

void * __attr_malloc
p_malloc_impl(pool_t pool, size_t size TRACE_ARGS_DECL);

#define p_malloc(pool, size) p_malloc_impl(pool, size TRACE_ARGS)
#define p_malloc_fwd(pool, size) p_malloc_impl(pool, size TRACE_ARGS_FWD)

void
p_free(pool_t pool, void *ptr);

void * __attr_malloc
p_calloc_impl(pool_t pool, size_t size TRACE_ARGS_DECL);

#define p_calloc(pool, size) p_calloc_impl(pool, size TRACE_ARGS)

char * __attr_malloc
p_memdup(pool_t pool, const void *src, size_t length);

char * __attr_malloc
p_strdup(pool_t pool, const char *src);

char * __attr_malloc
p_strndup_impl(pool_t pool, const char *src, size_t length TRACE_ARGS_DECL);

#define p_strndup(pool, src, length) p_strndup_impl(pool, src, length TRACE_ARGS)

char * __attr_malloc __attr_printf(2, 3)
p_sprintf(pool_t pool, const char *fmt, ...);

char * __attr_malloc
p_strcat(pool_t pool, const char *s, ...);

char * __attr_malloc
p_strncat(pool_t pool, const char *s, size_t length, ...);

#endif
