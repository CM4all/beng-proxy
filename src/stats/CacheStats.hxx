// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "memory/AllocatorStats.hxx"

#include <cstdint>

struct CacheStats {
	AllocatorStats allocator;

	uint_least64_t skips, misses, stores, hits;

	constexpr CacheStats &operator+=(const CacheStats &other) noexcept {
		allocator += other.allocator;
		skips += other.skips;
		misses += other.misses;
		stores += other.stores;
		hits += other.hits;
		return *this;
	}
};
