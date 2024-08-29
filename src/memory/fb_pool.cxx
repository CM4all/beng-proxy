// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "fb_pool.hxx"
#include "memory/SlicePool.hxx"

#include <cassert>

static SlicePool *fb_pool;

void
fb_pool_init() noexcept
{
	assert(fb_pool == nullptr);

	fb_pool = new SlicePool(FB_SIZE, 256, "io_buffers");
}

void
fb_pool_deinit() noexcept
{
	assert(fb_pool != nullptr);

	delete fb_pool;
	fb_pool = nullptr;
}

void
fb_pool_fork_cow(bool inherit) noexcept
{
	assert(fb_pool != nullptr);

	fb_pool->ForkCow(inherit);
}

SlicePool &
fb_pool_get() noexcept
{
	assert(fb_pool != nullptr);

	return *fb_pool;
}

void
fb_pool_compress() noexcept
{
	assert(fb_pool != nullptr);

	fb_pool->Compress();
}
