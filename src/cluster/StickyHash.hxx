// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>

/**
 * A type which can store a hash for choosing a cluster member.  Zero
 * is a special value for "sticky disabled"
 */
typedef uint32_t sticky_hash_t;

static constexpr sticky_hash_t
CombineStickyHashes(sticky_hash_t a, sticky_hash_t b) noexcept
{
	// TODO is XOR good enough to combine the two hashes?
	return a ^ b;
}
