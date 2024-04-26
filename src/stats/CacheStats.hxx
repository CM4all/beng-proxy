// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "memory/AllocatorStats.hxx"

struct CacheStats {
	AllocatorStats allocator;

	constexpr CacheStats &operator+=(const CacheStats &other) noexcept {
		allocator += other.allocator;
		return *this;
	}
};
