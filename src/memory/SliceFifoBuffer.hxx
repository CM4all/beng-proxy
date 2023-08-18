// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "SliceAllocation.hxx"
#include "util/ForeignFifoBuffer.hxx"

class SlicePool;
class SliceArea;

class SliceFifoBuffer : public ForeignFifoBuffer<std::byte> {
	SliceAllocation allocation;

public:
	SliceFifoBuffer() noexcept:ForeignFifoBuffer<std::byte>(nullptr) {}

	explicit SliceFifoBuffer(SlicePool &pool) noexcept
		:ForeignFifoBuffer<std::byte>(nullptr) {
		Allocate(pool);
	}

	SliceFifoBuffer(SliceFifoBuffer &&src) noexcept
		:ForeignFifoBuffer(std::move(src)),
		 allocation(std::move(src.allocation))
	{
		src.SetNull();
	}

	void swap(SliceFifoBuffer &other) noexcept {
		using std::swap;
		ForeignFifoBuffer<std::byte>::swap(other);
		swap(allocation, other.allocation);
	}

	friend void swap(SliceFifoBuffer &a, SliceFifoBuffer &b) noexcept {
		a.swap(b);
	}

	void Allocate(SlicePool &pool) noexcept;
	void Free() noexcept;

	bool IsDefinedAndFull() const noexcept {
		return IsDefined() && IsFull();
	}

	void AllocateIfNull(SlicePool &pool) noexcept {
		if (IsNull())
			Allocate(pool);
	}

	void FreeIfDefined() noexcept {
		if (IsDefined())
			Free();
	}

	void FreeIfEmpty() noexcept {
		if (empty())
			FreeIfDefined();
	}

	/**
	 * If this buffer is empty, free the buffer and reallocate a new
	 * one.  This is useful to work around #SliceArea fragmentation.
	 */
	void CycleIfEmpty(SlicePool &pool) noexcept {
		if (IsDefined() && empty()) {
			Free();
			Allocate(pool);
		}
	}

	using ForeignFifoBuffer<std::byte>::MoveFrom;

	/**
	 * Move as much data as possible from the specified buffer.  If
	 * the destination buffer is empty, the buffers are swapped.  Care
	 * is taken that neither buffer suddenly becomes nulled
	 * afterwards, because some callers may not be prepared for this.
	 *
	 * Note: this method has been removed because it was not
	 * possible to implement it to be safe against unallocated
	 * instances.  Use MoveFromAllow*() instead.
	 */
	void MoveFrom(SliceFifoBuffer &src) noexcept = delete;

	/**
	 * Like MoveFrom(), but allow the destination to be nulled.  This
	 * is useful when #src can be freed, but this object cannot.
	 */
	void MoveFromAllowNull(SliceFifoBuffer &src) noexcept {
		if (empty() && (!src.empty() || !IsNull()))
			/* optimized special case: swap buffer pointers instead of
			   copying data */
			swap(src);
		else
			ForeignFifoBuffer<std::byte>::MoveFrom(src);
	}

	/**
	 * Like MoveFrom(), but allow the source to be nulled.  This is
	 * useful when this object can be freed, but #src cannot.
	 */
	void MoveFromAllowSrcNull(SliceFifoBuffer &src) noexcept {
		if (empty() && (!src.empty() || IsNull()))
			/* optimized special case: swap buffer pointers instead of
			   copying data */
			swap(src);
		else
			ForeignFifoBuffer<std::byte>::MoveFrom(src);
	}

	/**
	 * Like MoveFrom(), but allow both to be nulled.
	 */
	void MoveFromAllowBothNull(SliceFifoBuffer &src) noexcept {
		if (empty())
			/* optimized special case: swap buffer pointers instead of
			   copying data */
			swap(src);
		else
			ForeignFifoBuffer<std::byte>::MoveFrom(src);
	}

	/**
	 * Swaps the two buffers if #src is nulled.  This is useful when
	 * #src can be freed, but this object cannot.
	 */
	void SwapIfNull(SliceFifoBuffer &src) noexcept {
		if (src.IsNull() && empty() && !IsNull())
			swap(src);
	}
};
