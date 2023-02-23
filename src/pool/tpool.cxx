// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "tpool.hxx"
#include "Ptr.hxx"

#include <assert.h>

struct pool *tpool_singleton;
unsigned tpool_users;

void
tpool_init(struct pool *parent) noexcept
{
	assert(tpool_singleton == nullptr);

	tpool_singleton = pool_new_linear(parent, "temporary_pool", 32768).release();
}

void
tpool_deinit() noexcept
{
	[[maybe_unused]] unsigned ref;
	ref = pool_unref(tpool_singleton);
	assert(ref == 0);
	tpool_singleton = nullptr;
}
