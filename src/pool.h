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
#include <stdbool.h>

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
    bool destroyed;
};

void
pool_notify(pool_t pool, struct pool_notify *notify);

static inline bool
pool_denotify(struct pool_notify *notify)
{
    if (notify->destroyed)
        return true;
    list_remove(&notify->siblings);
    return false;
}
#endif

#ifndef NDEBUG

void
pool_ref_notify_impl(pool_t pool, struct pool_notify *notify TRACE_ARGS_DECL);

void
pool_unref_denotify_impl(pool_t pool, struct pool_notify *notify
                         TRACE_ARGS_DECL);

/**
 * Do a "checked" pool reference.
 */
#define pool_ref_notify(pool, notify) \
    pool_ref_notify_impl(pool, notify TRACE_ARGS)

/**
 * Do a "checked" pool unreference.  If the pool has been destroyed,
 * an assertion will fail.  Double frees are also caught.
 */
#define pool_unref_denotify(pool, notify) \
    pool_unref_denotify_impl(pool, notify TRACE_ARGS)

#else
#define pool_ref_notify(pool, notify) pool_ref(pool)
#define pool_unref_denotify(pool, notify) pool_unref(pool)
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

static inline void
pool_attach(__attr_unused pool_t pool, __attr_unused const void *p,
            __attr_unused const char *name)
{
}

static inline void
pool_attach_checked(__attr_unused pool_t pool, __attr_unused const void *p,
                    __attr_unused const char *name)
{
}

static inline void
pool_detach(__attr_unused pool_t pool, __attr_unused const void *p)
{
}

static inline void
pool_detach_checked(__attr_unused pool_t pool, __attr_unused const void *p)
{
}

static inline const char *
pool_attachment_name(__attr_unused pool_t pool, __attr_unused const void *p)
{
    return NULL;
}

#else

void
pool_trash(pool_t pool);

void
pool_commit(void);

bool
pool_contains(pool_t pool, const void *ptr, size_t size);

/**
 * Attach an opaque object to the pool.  It must be detached before
 * the pool is destroyed.  This is used in debugging mode to track
 * whether all external objects have been destroyed.
 */
void
pool_attach(pool_t pool, const void *p, const char *name);

/**
 * Same as pool_attach(), but checks if the object is already
 * registered.
 */
void
pool_attach_checked(pool_t pool, const void *p, const char *name);

void
pool_detach(pool_t pool, const void *p);

void
pool_detach_checked(pool_t pool, const void *p);

const char *
pool_attachment_name(pool_t pool, const void *p);

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
p_free(pool_t pool, const void *ptr);

void * __attr_malloc
p_calloc_impl(pool_t pool, size_t size TRACE_ARGS_DECL);

#define p_calloc(pool, size) p_calloc_impl(pool, size TRACE_ARGS)

char * __attr_malloc
p_memdup(pool_t pool, const void *src, size_t length);

char * __attr_malloc
p_strdup(pool_t pool, const char *src);

static inline const char *
p_strdup_checked(pool_t pool, const char *s)
{
    return s == NULL ? NULL : p_strdup(pool, s);
}

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
