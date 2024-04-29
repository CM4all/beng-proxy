// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Node selection by cookie.
 */

#pragma once

#include "cluster/StickyHash.hxx"

#include <cassert>

class StringMap;

/**
 * Extract a node cookie from the request headers.
 */
sticky_hash_t
lb_cookie_get(const StringMap &request_headers) noexcept;

/**
 * Select a random worker.
 *
 * @param n the number of nodes in the cluster
 * @return a random number between 1 and n (both including)
 */
sticky_hash_t
lb_cookie_generate(unsigned n) noexcept;

/**
 * Calculate the next worker number.
 */
static inline unsigned
lb_cookie_next(unsigned n, unsigned i) noexcept
{
	assert(n >= 2);
	assert(i >= 1 && i <= n);

	return (i % n) + 1;
}
