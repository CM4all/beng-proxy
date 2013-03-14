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

struct pool;
struct slice_pool;

#ifndef __cplusplus

struct pool_mark {
    /**
     * The area that was current when the mark was set.
     */
    struct linear_pool_area *area;

    /**
     * The area before #area.  This is used to dispose areas that were
     * inserted before the current area due to a large allocation.
     */
    struct linear_pool_area *prev;

    /**
     * The position within the current area when the mark was set.
     */
    size_t position;

#ifndef NDEBUG
    /**
     * Used in an assertion: if the pool was empty before pool_mark(),
     * it must be empty again after pool_rewind().
     */
    bool was_empty;
#endif
};

#endif

void
pool_recycler_clear(void);

gcc_malloc
struct pool *
pool_new_libc(struct pool *parent, const char *name);

gcc_malloc
struct pool *
pool_new_linear(struct pool *parent, const char *name, size_t initial_size);

gcc_malloc
struct pool *
pool_new_slice(struct pool *parent, const char *name,
               struct slice_pool *slice_pool);

#ifdef NDEBUG

#define pool_set_major(pool)

#else

void
pool_set_major(struct pool *pool);

#endif

void
pool_ref_impl(struct pool *pool TRACE_ARGS_DECL);

#define pool_ref(pool) pool_ref_impl(pool TRACE_ARGS)
#define pool_ref_fwd(pool) pool_ref_impl(pool TRACE_ARGS_FWD)

unsigned
pool_unref_impl(struct pool *pool TRACE_ARGS_DECL);

#define pool_unref(pool) pool_unref_impl(pool TRACE_ARGS)
#define pool_unref_fwd(pool) pool_unref_impl(pool TRACE_ARGS_FWD)

/**
 * Returns the total size of all allocations in this pool.
 */
gcc_pure
size_t
pool_netto_size(const struct pool *pool);

/**
 * Returns the total amount of memory allocated by this pool.
 */
gcc_pure
size_t
pool_brutto_size(const struct pool *pool);

/**
 * Returns the total size of this pool and all of its descendants
 * (recursively).
 */
gcc_pure
size_t
pool_recursive_netto_size(const struct pool *pool);

gcc_pure
size_t
pool_recursive_brutto_size(const struct pool *pool);

/**
 * Returns the total size of all descendants of this pool (recursively).
 */
gcc_pure
size_t
pool_children_netto_size(const struct pool *pool);

gcc_pure
size_t
pool_children_brutto_size(const struct pool *pool);

void
pool_dump_tree(const struct pool *pool);

#ifndef NDEBUG
#include <inline/list.h>

struct pool_notify {
    struct list_head siblings;

    struct pool *pool;

    const char *name;

    bool destroyed, registered;

#ifdef TRACE
    const char *file;
    int line;
#endif
};

void
pool_notify(struct pool *pool, struct pool_notify *notify);

bool
pool_denotify(struct pool_notify *notify);

/**
 * Hands over control from an existing #pool_notify to a new one.  The
 * old one is unregistered.
 */
void
pool_notify_move(struct pool *pool, struct pool_notify *src,
                 struct pool_notify *dest);

#endif

#ifndef NDEBUG

void
pool_ref_notify_impl(struct pool *pool, struct pool_notify *notify TRACE_ARGS_DECL);

void
pool_unref_denotify_impl(struct pool *pool, struct pool_notify *notify
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
pool_trash(gcc_unused struct pool *pool)
{
}

static inline void
pool_commit(void)
{
}

static inline void
pool_attach(gcc_unused struct pool *pool, gcc_unused const void *p,
            gcc_unused const char *name)
{
}

static inline void
pool_attach_checked(gcc_unused struct pool *pool, gcc_unused const void *p,
                    gcc_unused const char *name)
{
}

static inline void
pool_detach(gcc_unused struct pool *pool, gcc_unused const void *p)
{
}

static inline void
pool_detach_checked(gcc_unused struct pool *pool, gcc_unused const void *p)
{
}

static inline const char *
pool_attachment_name(gcc_unused struct pool *pool, gcc_unused const void *p)
{
    return NULL;
}

#else

void
pool_trash(struct pool *pool);

void
pool_commit(void);

bool
pool_contains(struct pool *pool, const void *ptr, size_t size);

/**
 * Attach an opaque object to the pool.  It must be detached before
 * the pool is destroyed.  This is used in debugging mode to track
 * whether all external objects have been destroyed.
 */
void
pool_attach(struct pool *pool, const void *p, const char *name);

/**
 * Same as pool_attach(), but checks if the object is already
 * registered.
 */
void
pool_attach_checked(struct pool *pool, const void *p, const char *name);

void
pool_detach(struct pool *pool, const void *p);

void
pool_detach_checked(struct pool *pool, const void *p);

const char *
pool_attachment_name(struct pool *pool, const void *p);

#endif

#ifndef __cplusplus

void
pool_mark(struct pool *pool, struct pool_mark *mark);

void
pool_rewind(struct pool *pool, const struct pool_mark *mark);

#endif

gcc_malloc
void *
p_malloc_impl(struct pool *pool, size_t size TRACE_ARGS_DECL);

#define p_malloc(pool, size) p_malloc_impl(pool, size TRACE_ARGS)
#define p_malloc_fwd(pool, size) p_malloc_impl(pool, size TRACE_ARGS_FWD)

void
p_free(struct pool *pool, const void *ptr);

gcc_malloc
void *
p_calloc_impl(struct pool *pool, size_t size TRACE_ARGS_DECL);

#define p_calloc(pool, size) p_calloc_impl(pool, size TRACE_ARGS)

gcc_malloc
void *
p_memdup_impl(struct pool *pool, const void *src, size_t length TRACE_ARGS_DECL);

#define p_memdup(pool, src, length) p_memdup_impl(pool, src, length TRACE_ARGS)
#define p_memdup_fwd(pool, src, length) p_memdup_impl(pool, src, length TRACE_ARGS_FWD)

gcc_malloc
char *
p_strdup_impl(struct pool *pool, const char *src TRACE_ARGS_DECL);

#define p_strdup(pool, src) p_strdup_impl(pool, src TRACE_ARGS)
#define p_strdup_fwd(pool, src) p_strdup_impl(pool, src TRACE_ARGS_FWD)

static inline const char *
p_strdup_checked(struct pool *pool, const char *s)
{
    return s == NULL ? NULL : p_strdup(pool, s);
}

gcc_malloc
char *
p_strndup_impl(struct pool *pool, const char *src, size_t length TRACE_ARGS_DECL);

#define p_strndup(pool, src, length) p_strndup_impl(pool, src, length TRACE_ARGS)
#define p_strndup_fwd(pool, src, length) p_strndup_impl(pool, src, length TRACE_ARGS_FWD)

gcc_malloc gcc_printf(2, 3)
char *
p_sprintf(struct pool *pool, const char *fmt, ...);

gcc_malloc
char *
p_strcat(struct pool *pool, const char *s, ...);

gcc_malloc
char *
p_strncat(struct pool *pool, const char *s, size_t length, ...);

#endif
