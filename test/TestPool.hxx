// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "pool/RootPool.hxx"

#include <assert.h>

class TestPool {
	RootPool root_pool;
	PoolPtr the_pool;

public:
	TestPool()
		:the_pool(pool_new_libc(root_pool, "test")) {}

	TestPool(const TestPool &) = delete;
	TestPool &operator=(const TestPool &) = delete;

	operator struct pool &() {
		assert(the_pool);

		return the_pool;
	}

	operator struct pool *() {
		assert(the_pool);

		return the_pool;
	}

	PoolPtr Steal() noexcept {
		assert(the_pool);

		return std::move(the_pool);
	}
};
