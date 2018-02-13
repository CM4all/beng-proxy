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
pool_recycler_clear() noexcept;

gcc_malloc gcc_returns_nonnull
struct pool *
pool_new_libc(struct pool *parent, const char *name) noexcept;

gcc_malloc gcc_returns_nonnull
struct pool *
pool_new_linear(struct pool *parent, const char *name,
                size_t initial_size) noexcept;

gcc_malloc gcc_returns_nonnull
struct pool *
pool_new_slice(struct pool *parent, const char *name,
               struct SlicePool *slice_pool) noexcept;

#ifdef NDEBUG

#define pool_set_major(pool)

#else

void
pool_set_major(struct pool *pool) noexcept;

#endif

void
pool_ref_impl(struct pool *pool TRACE_ARGS_DECL) noexcept;

#define pool_ref(pool) pool_ref_impl(pool TRACE_ARGS)
#define pool_ref_fwd(pool) pool_ref_impl(pool TRACE_ARGS_FWD)

unsigned
pool_unref_impl(struct pool *pool TRACE_ARGS_DECL) noexcept;

#define pool_unref(pool) pool_unref_impl(pool TRACE_ARGS)
#define pool_unref_fwd(pool) pool_unref_impl(pool TRACE_ARGS_FWD)

class LinearPool {
    struct pool &p;

public:
    LinearPool(struct pool &parent, const char *name,
               size_t initial_size) noexcept
        :p(*pool_new_linear(&parent, name, initial_size)) {}

    ~LinearPool() noexcept {
        gcc_unused auto ref = pool_unref(&p);
#ifndef NDEBUG
        assert(ref == 0);
#endif
    }

    struct pool &get() noexcept {
        return p;
    }

    operator struct pool &() noexcept {
        return p;
    }

    operator struct pool *() noexcept {
        return &p;
    }
};

/**
 * Returns the total size of all allocations in this pool.
 */
gcc_pure
size_t
pool_netto_size(const struct pool *pool) noexcept;

/**
 * Returns the total amount of memory allocated by this pool.
 */
gcc_pure
size_t
pool_brutto_size(const struct pool *pool) noexcept;

/**
 * Returns the total size of this pool and all of its descendants
 * (recursively).
 */
gcc_pure
size_t
pool_recursive_netto_size(const struct pool *pool) noexcept;

gcc_pure
size_t
pool_recursive_brutto_size(const struct pool *pool) noexcept;

/**
 * Returns the total size of all descendants of this pool (recursively).
 */
gcc_pure
size_t
pool_children_netto_size(const struct pool *pool) noexcept;

gcc_pure
size_t
pool_children_brutto_size(const struct pool *pool) noexcept;

AllocatorStats
pool_children_stats(const struct pool &pool) noexcept;

void
pool_dump_tree(const struct pool &pool) noexcept;

#ifndef NDEBUG
#include <boost/intrusive/list.hpp>

struct pool_notify_state final
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {

    struct pool *pool;

    const char *name;

    bool destroyed;

#ifdef TRACE
    const char *file;
    int line;

    const char *destroyed_file;
    int destroyed_line;
#endif
};

void
pool_notify(struct pool *pool, struct pool_notify_state *notify) noexcept;

class PoolNotify {
    struct pool_notify_state state;

public:
    explicit PoolNotify(struct pool &pool) noexcept {
        pool_notify(&pool, &state);
    }

    PoolNotify(const PoolNotify &) = delete;
    PoolNotify &operator=(const PoolNotify &) = delete;

    bool IsDestroyed() noexcept {
        return state.destroyed;
    }
};

#endif

class ScopePoolRef {
    struct pool &pool;

#ifdef TRACE
    const char *const file;
    unsigned line;
#endif

public:
    explicit ScopePoolRef(struct pool &_pool TRACE_ARGS_DECL_) noexcept
        :pool(_pool)
         TRACE_ARGS_INIT
    {
        pool_ref_fwd(&_pool);
    }

    ScopePoolRef(const ScopePoolRef &) = delete;

    ~ScopePoolRef() noexcept {
        pool_unref_fwd(&pool);
    }

    operator struct pool &() noexcept {
        return pool;
    }

    operator struct pool *() noexcept {
        return &pool;
    }
};

/**
 * A reference-holding pointer to a "struct pool".
 */
class PoolPtr {
    struct pool *value = nullptr;

public:
    PoolPtr() = default;

    explicit PoolPtr(struct pool &_value) noexcept
        :value(&_value) {
        pool_ref(value);
    }

    PoolPtr(const PoolPtr &src) noexcept
        :value(src.value) {
        if (value != nullptr)
            pool_ref(value);
    }

    PoolPtr(PoolPtr &&src) noexcept
        :value(std::exchange(src.value, nullptr)) {}

    ~PoolPtr() noexcept {
        if (value != nullptr)
            pool_unref(value);
    }

    PoolPtr &operator=(const PoolPtr &src) noexcept {
        if (value != nullptr)
            pool_unref(value);
        value = src.value;
        if (value != nullptr)
            pool_ref(value);
        return *this;
    }

    PoolPtr &operator=(PoolPtr &&src) noexcept {
        using std::swap;
        swap(value, src.value);
        return *this;
    }

    operator bool() const noexcept {
        return value;
    }

    operator struct pool &() const noexcept {
        return *value;
    }

    operator struct pool *() const noexcept {
        return value;
    }
};

/**
 * Base class for classes which hold a reference to a #pool.
 */
class PoolHolder {
protected:
    const PoolPtr pool;

    template<typename P>
    explicit PoolHolder(P &&_pool) noexcept
        :pool(std::forward<P>(_pool))
    {
    }

    PoolHolder(const PoolHolder &) = delete;
    PoolHolder &operator=(const PoolHolder &) = delete;

    struct pool &GetPool() noexcept {
        return *pool;
    }
};

#ifdef NDEBUG

static inline void
pool_trash(gcc_unused struct pool *pool) noexcept
{
}

static inline void
pool_commit() noexcept
{
}

static inline void
pool_attach(gcc_unused struct pool *pool, gcc_unused const void *p,
            gcc_unused const char *name) noexcept
{
}

static inline void
pool_attach_checked(gcc_unused struct pool *pool, gcc_unused const void *p,
                    gcc_unused const char *name) noexcept
{
}

static inline void
pool_detach(gcc_unused struct pool *pool, gcc_unused const void *p) noexcept
{
}

static inline void
pool_detach_checked(gcc_unused struct pool *pool,
                    gcc_unused const void *p) noexcept
{
}

static inline const char *
pool_attachment_name(gcc_unused struct pool *pool,
                     gcc_unused const void *p) noexcept
{
    return NULL;
}

#else

void
pool_trash(struct pool *pool) noexcept;

void
pool_commit() noexcept;

bool
pool_contains(const struct pool &pool, const void *ptr, size_t size) noexcept;

/**
 * Attach an opaque object to the pool.  It must be detached before
 * the pool is destroyed.  This is used in debugging mode to track
 * whether all external objects have been destroyed.
 */
void
pool_attach(struct pool *pool, const void *p, const char *name) noexcept;

/**
 * Same as pool_attach(), but checks if the object is already
 * registered.
 */
void
pool_attach_checked(struct pool *pool, const void *p,
                    const char *name) noexcept;

void
pool_detach(struct pool *pool, const void *p) noexcept;

void
pool_detach_checked(struct pool *pool, const void *p) noexcept;

const char *
pool_attachment_name(struct pool *pool, const void *p) noexcept;

#endif

void
pool_mark(struct pool *pool, struct pool_mark_state *mark) noexcept;

void
pool_rewind(struct pool *pool, const struct pool_mark_state *mark) noexcept;

class AutoRewindPool {
    struct pool &pool;
    pool_mark_state mark;

public:
    AutoRewindPool(struct pool &_pool) noexcept:pool(_pool) {
        pool_mark(&pool, &mark);
    }

    AutoRewindPool(const AutoRewindPool &) = delete;

    ~AutoRewindPool() noexcept {
        pool_rewind(&pool, &mark);
    }
};

gcc_malloc gcc_returns_nonnull
void *
p_malloc_impl(struct pool *pool, size_t size TRACE_ARGS_DECL) noexcept;

#define p_malloc(pool, size) p_malloc_impl(pool, size TRACE_ARGS)
#define p_malloc_fwd(pool, size) p_malloc_impl(pool, size TRACE_ARGS_FWD)

void
p_free(struct pool *pool, const void *ptr) noexcept;

gcc_malloc gcc_returns_nonnull
void *
p_memdup_impl(struct pool *pool, const void *src, size_t length
              TRACE_ARGS_DECL) noexcept;

#define p_memdup(pool, src, length) p_memdup_impl(pool, src, length TRACE_ARGS)
#define p_memdup_fwd(pool, src, length) p_memdup_impl(pool, src, length TRACE_ARGS_FWD)

gcc_malloc gcc_returns_nonnull
char *
p_strdup_impl(struct pool *pool, const char *src TRACE_ARGS_DECL) noexcept;

#define p_strdup(pool, src) p_strdup_impl(pool, src TRACE_ARGS)
#define p_strdup_fwd(pool, src) p_strdup_impl(pool, src TRACE_ARGS_FWD)

static inline const char *
p_strdup_checked(struct pool *pool, const char *s) noexcept
{
    return s == NULL ? NULL : p_strdup(pool, s);
}

gcc_malloc gcc_returns_nonnull
char *
p_strdup_lower_impl(struct pool *pool, const char *src
                    TRACE_ARGS_DECL) noexcept;

#define p_strdup_lower(pool, src) p_strdup_lower_impl(pool, src TRACE_ARGS)
#define p_strdup_lower_fwd(pool, src) p_strdup_lower_impl(pool, src TRACE_ARGS_FWD)

gcc_malloc gcc_returns_nonnull
char *
p_strndup_impl(struct pool *pool, const char *src, size_t length
               TRACE_ARGS_DECL) noexcept;

#define p_strndup(pool, src, length) p_strndup_impl(pool, src, length TRACE_ARGS)
#define p_strndup_fwd(pool, src, length) p_strndup_impl(pool, src, length TRACE_ARGS_FWD)

gcc_malloc gcc_returns_nonnull
char *
p_strndup_lower_impl(struct pool *pool, const char *src, size_t length
                     TRACE_ARGS_DECL) noexcept;

#define p_strndup_lower(pool, src, length) p_strndup_lower_impl(pool, src, length TRACE_ARGS)
#define p_strndup_lower_fwd(pool, src, length) p_strndup_lower_impl(pool, src, length TRACE_ARGS_FWD)

gcc_malloc gcc_returns_nonnull gcc_printf(2, 3)
char *
p_sprintf(struct pool *pool, const char *fmt, ...) noexcept;

gcc_malloc gcc_returns_nonnull
char *
p_strcat(struct pool *pool, const char *s, ...) noexcept;

gcc_malloc gcc_returns_nonnull
char *
p_strncat(struct pool *pool, const char *s, size_t length, ...) noexcept;

template<typename T>
gcc_malloc gcc_returns_nonnull
T *
PoolAlloc(pool &p) noexcept
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
gcc_malloc gcc_returns_nonnull
T *
PoolAlloc(pool &p, size_t n) noexcept
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
gcc_malloc gcc_returns_nonnull
inline void *
PoolAlloc<void>(pool &p, size_t n) noexcept
{
    return p_malloc(&p, n);
}

template<typename T, typename... Args>
gcc_malloc gcc_returns_nonnull
T *
NewFromPool(pool &p, Args&&... args)
{
    void *t = p_malloc(&p, sizeof(T));
    return ::new(t) T(std::forward<Args>(args)...);
}

template<typename T>
void
DeleteFromPool(struct pool &pool, T *t) noexcept
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
    explicit PoolDisposer(struct pool &_p) noexcept:p(_p) {}

    template<typename T>
    void operator()(T *t) noexcept {
        DeleteFromPool(p, t);
    }
};

template<typename T>
void
DeleteUnrefPool(struct pool &pool, T *t) noexcept
{
    DeleteFromPool(pool, t);
    pool_unref(&pool);
}

template<typename T>
void
DeleteUnrefTrashPool(struct pool &pool, T *t) noexcept
{
    pool_trash(&pool);
    DeleteUnrefPool(pool, t);
}

class PoolAllocator {
    struct pool &pool;

public:
    explicit constexpr PoolAllocator(struct pool &_pool) noexcept
        :pool(_pool) {}

    void *Allocate(size_t size) noexcept {
        return p_malloc(&pool, size);
    }

    char *DupString(const char *p) noexcept {
        return p_strdup(&pool, p);
    }

    void Free(void *p) noexcept {
        p_free(&pool, p);
    }

    template<typename T, typename... Args>
    T *New(Args&&... args) {
        return NewFromPool<T>(pool, std::forward<Args>(args)...);
    }

    template<typename T>
    void Delete(T *t) noexcept {
        DeleteFromPool(pool, t);
    }
};

gcc_malloc gcc_returns_nonnull
char *
p_strdup_impl(struct pool &pool, StringView src TRACE_ARGS_DECL) noexcept;

gcc_malloc gcc_returns_nonnull
char *
p_strdup_lower_impl(struct pool &pool, StringView src
                    TRACE_ARGS_DECL) noexcept;

#endif
