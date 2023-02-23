// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "trace.h"

#include <cstddef>
#include <utility>

/**
 * A reference-holding pointer to a "struct pool".
 */
class PoolPtr {
	struct pool *value = nullptr;

#ifdef ENABLE_TRACE
	const char *file = nullptr;
	unsigned line = 0;
#endif

public:
	PoolPtr() = default;

	explicit PoolPtr(struct pool &_value TRACE_ARGS_DEFAULT) noexcept;
	PoolPtr(const PoolPtr &src TRACE_ARGS_DEFAULT) noexcept;

	struct Donate {};
	static Donate donate;

	/**
	 * Donate a pool reference to a newly constructed #PoolPtr.  It
	 * will not create another reference, but will unreference it in
	 * its destructor.
	 */
	PoolPtr(Donate, struct pool &_value TRACE_ARGS_DEFAULT_) noexcept
		:value(&_value)
		 TRACE_ARGS_INIT
	{
	}

	PoolPtr(Donate, const PoolPtr &_value) noexcept = delete;

	PoolPtr(PoolPtr &&src) noexcept
		:value(std::exchange(src.value, nullptr))
		 TRACE_ARGS_INIT_FROM(src)
	{
	}

	~PoolPtr() noexcept;

	PoolPtr &operator=(const PoolPtr &src) noexcept;

	PoolPtr &operator=(PoolPtr &&src) noexcept {
		using std::swap;
		swap(value, src.value);
#ifdef ENABLE_TRACE
		file = src.file;
		line = src.line;
#endif
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

	void reset() noexcept;

	/**
	 * Return the value, releasing ownership.
	 */
	struct pool *release() noexcept {
		return std::exchange(value, nullptr);
	}

	void *Allocate(std::size_t size) const noexcept;
};

/**
 * Create a newly allocated object and move the pool reference into it
 * as the first parameter.
 */
template<typename T, typename... Args>
[[gnu::malloc]] [[gnu::returns_nonnull]]
T *
NewFromPool(PoolPtr &&p, Args&&... args)
{
	void *t = p.Allocate(sizeof(T));
	return ::new(t) T(std::move(p), std::forward<Args>(args)...);
}
