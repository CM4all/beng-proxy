// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "memory/SliceAllocation.hxx"

#include <cassert>
#include <cstddef>
#include <span>
#include <utility>

class SliceArea;

class DefaultChunkAllocator {
	SliceAllocation allocation;

public:
	DefaultChunkAllocator() = default;
	DefaultChunkAllocator(DefaultChunkAllocator &&src) noexcept = default;

	DefaultChunkAllocator &operator=(const DefaultChunkAllocator &) = delete;

#ifndef NDEBUG
	~DefaultChunkAllocator() noexcept;
#endif

	friend void swap(DefaultChunkAllocator &a,
			 DefaultChunkAllocator &b) noexcept {
		using std::swap;
		swap(a.allocation, b.allocation);
	}

	std::span<std::byte> Allocate() noexcept;
	void Free() noexcept;

	bool IsDefined() const noexcept {
		return allocation.IsDefined();
	}

	std::size_t size() const noexcept {
		assert(allocation.IsDefined());

		return allocation.size;
	}

	static std::size_t GetChunkSize() noexcept;
};
