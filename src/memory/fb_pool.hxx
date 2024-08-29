// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * An allocator for fifo_buffer objects that can return unused memory
 * back to the kernel.
 */

#pragma once

#include <cstddef>

class SlicePool;

static constexpr size_t FB_SIZE = 32768;

/**
 * Global initialization.
 */
void
fb_pool_init() noexcept;

/**
 * Global deinitialization.
 */
void
fb_pool_deinit() noexcept;

void
fb_pool_fork_cow(bool inherit) noexcept;

[[gnu::const]]
SlicePool &
fb_pool_get() noexcept;

/**
 * Give free memory back to the kernel.  The library will
 * automatically do this once in a while.  This call forces immediate
 * cleanup.
 */
void
fb_pool_compress() noexcept;

class ScopeFbPoolInit {
public:
	ScopeFbPoolInit() noexcept {
		fb_pool_init();
	}

	~ScopeFbPoolInit() noexcept {
		fb_pool_deinit();
	}
};
