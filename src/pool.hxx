/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Memory pool.
 */

#ifndef BENG_PROXY_POOL_HXX
#define BENG_PROXY_POOL_HXX

#include "trace.h"

#include "util/Compiler.h"

#include <type_traits>
#include <utility>
#include <new>

#ifndef NDEBUG
#include <assert.h>
#endif

#include <stddef.h>
#include <stdbool.h>

struct pool;
struct SlicePool;
struct AllocatorStats;

struct pool_mark_state {
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

struct StringView;

void
pool_recycler_clear();

gcc_malloc
struct pool *
pool_new_libc(struct pool *parent, const char *name);

gcc_malloc
struct pool *
pool_new_linear(struct pool *parent, const char *name, size_t initial_size);

gcc_malloc
struct pool *
pool_new_slice(struct pool *parent, const char *name,
               struct SlicePool *slice_pool);

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

class LinearPool {
    struct pool &p;

public:
    LinearPool(struct pool &parent, const char *name, size_t initial_size)
        :p(*pool_new_linear(&parent, name, initial_size)) {}

    ~LinearPool() {
        gcc_unused auto ref = pool_unref(&p);
#ifndef NDEBUG
        assert(ref == 0);
#endif
    }

    struct pool &get() {
        return p;
    }

    operator struct pool &() {
        return p;
    }

    operator struct pool *() {
        return &p;
    }
};

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

AllocatorStats
pool_children_stats(const struct pool &pool);

void
pool_dump_tree(const struct pool &pool);

#ifndef NDEBUG
#include <boost/intrusive/list.hpp>

struct pool_notify_state final
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    struct pool *pool;

    const char *name;

    bool destroyed, registered;

#ifdef TRACE
    const char *file;
    int line;

    const char *destroyed_file;
    int destroyed_line;
#endif
};

void
pool_notify(struct pool *pool, struct pool_notify_state *notify);

bool
pool_denotify(struct pool_notify_state *notify);

/**
 * Hands over control from an existing #pool_notify to a new one.  The
 * old one is unregistered.
 */
void
pool_notify_move(struct pool *pool, struct pool_notify_state *src,
                 struct pool_notify_state *dest);

class PoolNotify {
    struct pool_notify_state state;

public:
    explicit PoolNotify(struct pool &pool) {
        pool_notify(&pool, &state);
    }

    PoolNotify(const PoolNotify &) = delete;

#ifndef NDEBUG
    ~PoolNotify() {
        assert(!state.registered);
    }
#endif

    bool Denotify() {
        return pool_denotify(&state);
    }
};

#endif

class ScopePoolRef {
    struct pool &pool;
#ifndef NDEBUG
    PoolNotify notify;
#endif

#ifdef TRACE
    const char *const file;
    unsigned line;
#endif

public:
    explicit ScopePoolRef(struct pool &_pool TRACE_ARGS_DECL_)
        :pool(_pool)
#ifndef NDEBUG
        , notify(_pool)
#endif
         TRACE_ARGS_INIT
    {
        pool_ref_fwd(&_pool);
    }

    ScopePoolRef(const ScopePoolRef &) = delete;

    ~ScopePoolRef() {
#ifndef NDEBUG
        notify.Denotify();
#endif
        pool_unref_fwd(&pool);
    }

    operator struct pool &() {
        return pool;
    }

    operator struct pool *() {
        return &pool;
    }
};

/**
 * Base class for classes which hold a reference to a #pool.
 */
class PoolHolder {
protected:
    struct pool &pool;

    explicit PoolHolder(struct pool &_pool)
        :pool(_pool)
    {
        pool_ref(&_pool);
    }

    PoolHolder(const PoolHolder &) = delete;
    PoolHolder &operator=(const PoolHolder &) = delete;

    ~PoolHolder() {
        pool_unref(&pool);
    }

    struct pool &GetPool() {
        return pool;
    }
};

#ifndef NDEBUG

void
pool_ref_notify_impl(struct pool *pool, struct pool_notify_state *notify TRACE_ARGS_DECL);

void
pool_unref_denotify_impl(struct pool *pool, struct pool_notify_state *notify
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
pool_commit()
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
pool_commit();

bool
pool_contains(const struct pool &pool, const void *ptr, size_t size);

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

void
pool_mark(struct pool *pool, struct pool_mark_state *mark);

void
pool_rewind(struct pool *pool, const struct pool_mark_state *mark);

class AutoRewindPool {
    struct pool &pool;
    pool_mark_state mark;

public:
    AutoRewindPool(struct pool &_pool):pool(_pool) {
        pool_mark(&pool, &mark);
    }

    AutoRewindPool(const AutoRewindPool &) = delete;

    ~AutoRewindPool() {
        pool_rewind(&pool, &mark);
    }
};

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
p_strdup_lower_impl(struct pool *pool, const char *src TRACE_ARGS_DECL);

#define p_strdup_lower(pool, src) p_strdup_lower_impl(pool, src TRACE_ARGS)
#define p_strdup_lower_fwd(pool, src) p_strdup_lower_impl(pool, src TRACE_ARGS_FWD)

gcc_malloc
char *
p_strndup_impl(struct pool *pool, const char *src, size_t length TRACE_ARGS_DECL);

#define p_strndup(pool, src, length) p_strndup_impl(pool, src, length TRACE_ARGS)
#define p_strndup_fwd(pool, src, length) p_strndup_impl(pool, src, length TRACE_ARGS_FWD)

gcc_malloc
char *
p_strndup_lower_impl(struct pool *pool, const char *src, size_t length TRACE_ARGS_DECL);

#define p_strndup_lower(pool, src, length) p_strndup_lower_impl(pool, src, length TRACE_ARGS)
#define p_strndup_lower_fwd(pool, src, length) p_strndup_lower_impl(pool, src, length TRACE_ARGS_FWD)

gcc_malloc gcc_printf(2, 3)
char *
p_sprintf(struct pool *pool, const char *fmt, ...);

gcc_malloc
char *
p_strcat(struct pool *pool, const char *s, ...);

gcc_malloc
char *
p_strncat(struct pool *pool, const char *s, size_t length, ...);

template<typename T>
T *
PoolAlloc(pool &p)
{
#if CLANG_OR_GCC_VERSION(5,0)
    static_assert(std::is_trivially_default_constructible<T>::value,
                  "Must be trivially constructible");
#else
    static_assert(std::has_trivial_default_constructor<T>::value,
                  "Must be trivially constructible");
#endif

    return (T *)p_malloc(&p, sizeof(T));
}

template<typename T>
T *
PoolAlloc(pool &p, size_t n)
{
#if CLANG_OR_GCC_VERSION(5,0)
    static_assert(std::is_trivially_default_constructible<T>::value,
                  "Must be trivially constructible");
#else
    static_assert(std::has_trivial_default_constructor<T>::value,
                  "Must be trivially constructible");
#endif

    return (T *)p_malloc(&p, sizeof(T) * n);
}

template<>
inline void *
PoolAlloc<void>(pool &p, size_t n)
{
    return p_malloc(&p, n);
}

template<typename T, typename... Args>
T *
NewFromPool(pool &p, Args&&... args)
{
    void *t = p_malloc(&p, sizeof(T));
    return ::new(t) T(std::forward<Args>(args)...);
}

template<typename T>
void
DeleteFromPool(struct pool &pool, T *t)
{
    t->~T();
    p_free(&pool, t);
}

/**
 * A disposer for boost::intrusive that invokes the DeleteFromPool()
 * on the given pointer.
 */
class PoolDisposer {
    struct pool &p;

public:
    explicit PoolDisposer(struct pool &_p):p(_p) {}

    template<typename T>
    void operator()(T *t) {
        DeleteFromPool(p, t);
    }
};

template<typename T>
void
DeleteUnrefPool(struct pool &pool, T *t)
{
    DeleteFromPool(pool, t);
    pool_unref(&pool);
}

template<typename T>
void
DeleteUnrefTrashPool(struct pool &pool, T *t)
{
    pool_trash(&pool);
    DeleteUnrefPool(pool, t);
}

class PoolAllocator {
    struct pool &pool;

public:
    explicit constexpr PoolAllocator(struct pool &_pool):pool(_pool) {}

    void *Allocate(size_t size) {
        return p_malloc(&pool, size);
    }

    char *DupString(const char *p) {
        return p_strdup(&pool, p);
    }

    void Free(void *p) {
        p_free(&pool, p);
    }

    template<typename T, typename... Args>
    T *New(Args&&... args) {
        return NewFromPool<T>(pool, std::forward<Args>(args)...);
    }

    template<typename T>
    void Delete(T *t) {
        DeleteFromPool(pool, t);
    }
};

gcc_malloc
char *
p_strdup_impl(struct pool &pool, StringView src TRACE_ARGS_DECL);

gcc_malloc
char *
p_strdup_lower_impl(struct pool &pool, StringView src TRACE_ARGS_DECL);

#endif
