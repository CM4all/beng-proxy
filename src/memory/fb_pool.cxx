// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "fb_pool.hxx"
#include "SlicePool.hxx"

#include <assert.h>

static SlicePool *fb_pool;

void
fb_pool_init()
{
	assert(fb_pool == nullptr);

	fb_pool = new SlicePool(FB_SIZE, 256, "io_buffers");
}

void
fb_pool_deinit(void)
{
	assert(fb_pool != nullptr);

	delete fb_pool;
	fb_pool = nullptr;
}

void
fb_pool_fork_cow(bool inherit)
{
	assert(fb_pool != nullptr);

	fb_pool->ForkCow(inherit);
}

SlicePool &
fb_pool_get()
{
	assert(fb_pool != nullptr);

	return *fb_pool;
}

void
fb_pool_compress(void)
{
	assert(fb_pool != nullptr);

	fb_pool->Compress();
}
