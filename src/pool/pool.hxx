// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Memory pool.
 */

#pragma once

#include "Type.hxx"
#include "trace.h"

#include <cstddef>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

struct pool;
class SlicePool;
struct AllocatorStats;
class PoolPtr;
class PoolLeakDetector;

void
pool_recycler_clear() noexcept;

/**
 * Create a new pool which cannot allocate anything; it only serves as
 * parent for other pools.
 */
PoolPtr
pool_new_dummy(struct pool *parent, const char *name) noexcept;

PoolPtr
pool_new_libc(struct pool *parent, const char *name) noexcept;

PoolPtr
pool_new_linear(struct pool *parent, const char *name,
		size_t initial_size) noexcept;

PoolPtr
pool_new_slice(struct pool *parent, const char *name,
	       SlicePool *slice_pool) noexcept;

#ifdef NDEBUG

#define pool_set_major(pool)

#else

void
pool_set_major(struct pool *pool) noexcept;

#endif

void
pool_ref(struct pool *pool TRACE_ARGS_DEFAULT) noexcept;

unsigned
pool_unref(struct pool *pool TRACE_ARGS_DEFAULT) noexcept;

/* not implemented - just here to detect bugs */
void
pool_ref(const PoolPtr &pool TRACE_ARGS_DEFAULT) noexcept;

/* not implemented - just here to detect bugs */
void
pool_unref(const PoolPtr &pool TRACE_ARGS_DEFAULT) noexcept;

/**
 * Returns the total size of all allocations in this pool.
 */
[[gnu::pure]]
size_t
pool_netto_size(const struct pool *pool) noexcept;

/**
 * Returns the total amount of memory allocated by this pool.
 */
[[gnu::pure]]
size_t
pool_brutto_size(const struct pool *pool) noexcept;

/**
 * Returns the total size of this pool and all of its descendants
 * (recursively).
 */
[[gnu::pure]]
size_t
pool_recursive_netto_size(const struct pool *pool) noexcept;

[[gnu::pure]]
size_t
pool_recursive_brutto_size(const struct pool *pool) noexcept;

/**
 * Returns the total size of all descendants of this pool (recursively).
 */
[[gnu::pure]]
size_t
pool_children_netto_size(const struct pool *pool) noexcept;

[[gnu::pure]]
size_t
pool_children_brutto_size(const struct pool *pool) noexcept;

AllocatorStats
pool_children_stats(const struct pool &pool) noexcept;

void
pool_dump_tree(const struct pool &pool) noexcept;

class ScopePoolRef {
	struct pool &pool;

#ifdef ENABLE_TRACE
	const char *const file;
	const unsigned line;
#endif

public:
	explicit ScopePoolRef(struct pool &_pool TRACE_ARGS_DEFAULT_) noexcept
		:pool(_pool)
		 TRACE_ARGS_INIT
	{
		pool_ref(&_pool TRACE_ARGS_FWD);
	}

	ScopePoolRef(const ScopePoolRef &) = delete;

	~ScopePoolRef() noexcept {
		pool_unref(&pool TRACE_ARGS_FWD);
	}

	operator struct pool &() noexcept {
		return pool;
	}

	operator struct pool *() noexcept {
		return &pool;
	}
};

#ifdef NDEBUG

static inline void
pool_trash(struct pool *) noexcept
{
}

static inline void
pool_commit() noexcept
{
}

#else

void
pool_trash(struct pool *pool) noexcept;

void
pool_commit() noexcept;

bool
pool_contains(const struct pool &pool, const void *ptr, size_t size) noexcept;

/**
 * Register a #PoolLeakDetector to the pool.
 */
void
pool_register_leak_detector(struct pool &pool, PoolLeakDetector &ld) noexcept;

#endif

/**
 * Free all allocations.
 */
void
pool_clear(struct pool &pool) noexcept;

[[gnu::malloc]] [[gnu::returns_nonnull]]
void *
p_malloc(struct pool *pool, size_t size
	 TYPE_ARG_DECL TRACE_ARGS_DEFAULT) noexcept;

[[gnu::malloc]] [[gnu::returns_nonnull]]
static inline void *
p_malloc_type(struct pool &pool, size_t size TYPE_ARG_DECL TRACE_ARGS_DEFAULT) noexcept
{
	return p_malloc(&pool, size TYPE_ARG_FWD TRACE_ARGS_FWD);
}

#ifndef NDEBUG

[[gnu::malloc]] [[gnu::returns_nonnull]]
static inline void *
p_malloc(struct pool *pool, size_t size TRACE_ARGS_DEFAULT) noexcept
{
	return p_malloc(pool, size TYPE_ARG_NULL TRACE_ARGS_FWD);
}

#endif

#define p_malloc_fwd(pool, size) p_malloc(pool, size TRACE_ARGS_FWD)

void
p_free(struct pool *pool, const void *ptr, size_t size) noexcept;

[[gnu::malloc]] [[gnu::returns_nonnull]]
void *
p_memdup(struct pool *pool, const void *src, size_t length
	 TRACE_ARGS_DEFAULT) noexcept;

#define p_memdup_fwd(pool, src, length) p_memdup(pool, src, length TRACE_ARGS_FWD)

[[gnu::malloc]] [[gnu::returns_nonnull]]
char *
p_strdup(struct pool *pool, const char *src TRACE_ARGS_DEFAULT) noexcept;

[[gnu::malloc]] [[gnu::returns_nonnull]]
char *
p_strndup(struct pool *pool, const char *src, size_t length
	  TRACE_ARGS_DEFAULT) noexcept;

#define p_strndup_fwd(pool, src, length) p_strndup(pool, src, length TRACE_ARGS_FWD)

template<typename T>
[[gnu::malloc]] [[gnu::returns_nonnull]]
T *
PoolAlloc(pool &p) noexcept
{
	static_assert(std::is_trivially_default_constructible<T>::value,
		      "Must be trivially constructible");

	return (T *)p_malloc_type(p, sizeof(T) TYPE_ARG(T));
}

template<typename T>
[[gnu::malloc]] [[gnu::returns_nonnull]]
T *
PoolAlloc(pool &p, size_t n) noexcept
{
	static_assert(std::is_trivially_default_constructible<T>::value,
		      "Must be trivially constructible");

	return (T *)p_malloc_type(p, sizeof(T) * n TYPE_ARG(T));
}

template<>
[[gnu::malloc]] [[gnu::returns_nonnull]]
inline void *
PoolAlloc<void>(pool &p, size_t n) noexcept
{
	return p_malloc(&p, n);
}

template<typename T, typename... Args>
[[gnu::malloc]] [[gnu::returns_nonnull]]
T *
NewFromPool(pool &p, Args&&... args)
{
	void *t = p_malloc_type(p, sizeof(T) TYPE_ARG(T));
	return ::new(t) T(std::forward<Args>(args)...);
}

template<typename T>
void
DeleteFromPool(struct pool &pool, T *t) noexcept
{
	t->~T();
	p_free(&pool, t, sizeof(*t));
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

/**
 * An overload without a pool; this one cannot free memory
 * (pool_new_libc() only) and has no assertions, but it calls the
 * object's destructor.  It can sometimes be useful to destruct
 * objects which don't have a pointer to the pool they were allocated
 * from.
 */
template<typename T>
void
DeleteFromPool(T *t) noexcept
{
	t->~T();
}

/**
 * Like #PoolDisposer, but calls the DeleteFromPool() overload without
 * a pool parameter.
 */
class NoPoolDisposer {
public:
	template<typename T>
	void operator()(T *t) noexcept {
		DeleteFromPool(t);
	}
};

[[gnu::malloc]] [[gnu::returns_nonnull]]
char *
p_strdup(struct pool &pool, std::string_view src TRACE_ARGS_DEFAULT) noexcept;

[[gnu::malloc]] [[gnu::returns_nonnull]]
char *
p_strdup_lower(struct pool &pool, std::string_view src
	       TRACE_ARGS_DEFAULT) noexcept;
