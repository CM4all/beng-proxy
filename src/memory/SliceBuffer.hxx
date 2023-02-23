// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "SliceAllocation.hxx"

#include <cassert>
#include <span>

class SlicePool;
class SliceArea;

/**
 * A buffer allocated from a #SlicePool which the caller can append
 * data to, until it is full.  This is a simplified version of
 * #SliceFifoBuffer which cannot consume any data.
 */
class SliceBuffer {
	SliceAllocation allocation;

	size_t fill;

public:
	SliceBuffer() = default;

	explicit SliceBuffer(SliceAllocation &&src) noexcept
		:allocation(std::move(src)), fill(0) {}

	SliceBuffer(SliceBuffer &&) noexcept = default;

	~SliceBuffer() noexcept {
		if (allocation.IsDefined())
			allocation.Free();
	}

	SliceBuffer &operator=(SliceBuffer &&) noexcept = default;

	SliceBuffer &operator=(SliceAllocation &&src) noexcept {
		allocation = std::move(src);
		fill = 0;
		return *this;
	}

	bool IsDefined() const noexcept {
		return allocation.IsDefined();
	}

	auto size() const noexcept {
		assert(IsDefined());

		return fill;
	}

	bool empty() const noexcept {
		return size() == 0;
	}

	std::span<const std::byte> Read() noexcept {
		assert(IsDefined());

		return {(const std::byte *)allocation.data, fill};
	}

	std::span<std::byte> Write() noexcept {
		assert(IsDefined());

		return {(std::byte *)allocation.data + fill, allocation.size - fill};
	}

	void Append(size_t n) noexcept {
		assert(IsDefined());

		fill += n;
	}

	SliceAllocation &&StealAllocation() noexcept {
		return std::move(allocation);
	}
};
