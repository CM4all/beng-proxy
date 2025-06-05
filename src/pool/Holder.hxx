// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Ptr.hxx"

/**
 * Base class for classes which hold a reference to a #pool.
 *
 * This works around an ordering problem with class #PoolPtr: if you
 * instead use #PoolPtr as an attribute inside a class allocated with
 * this pool, its destructor will be called before destruction of the
 * whole object has finished, leading to use-after-free bugs.  By
 * deriving from this class, you are able to call its destructor at
 * the very end.  For this to happen, this must be the innermost and
 * first subclass.
 */
class PoolHolder {
protected:
	const PoolPtr pool;

	template<typename P>
	explicit PoolHolder(P &&_pool
			    TRACE_ARGS_DEFAULT) noexcept
		:pool(std::forward<P>(_pool) TRACE_ARGS_FWD)
	{
	}

	PoolHolder(const PoolHolder &) = delete;
	PoolHolder &operator=(const PoolHolder &) = delete;

	struct pool &GetPool() noexcept {
		return *pool;
	}
};
