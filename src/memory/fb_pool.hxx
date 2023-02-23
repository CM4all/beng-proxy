// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * An allocator for fifo_buffer objects that can return unused memory
 * back to the kernel.
 */

#pragma once

#include <stddef.h>

class SlicePool;

static constexpr size_t FB_SIZE = 32768;

/**
 * Global initialization.
 */
void
fb_pool_init();

/**
 * Global deinitialization.
 */
void
fb_pool_deinit();

void
fb_pool_fork_cow(bool inherit);

[[gnu::const]]
SlicePool &
fb_pool_get();

/**
 * Give free memory back to the kernel.  The library will
 * automatically do this once in a while.  This call forces immediate
 * cleanup.
 */
void
fb_pool_compress(void);

class ScopeFbPoolInit {
public:
	ScopeFbPoolInit() {
		fb_pool_init();
	}

	~ScopeFbPoolInit() {
		fb_pool_deinit();
	}
};
