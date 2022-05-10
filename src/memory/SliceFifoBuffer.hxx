/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "SliceAllocation.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <stdint.h>

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
	 */
	void MoveFrom(SliceFifoBuffer &src) noexcept {
		if (empty() && !IsNull() && !src.IsNull())
			/* optimized special case: swap buffer pointers instead of
			   copying data */
			swap(src);
		else
			ForeignFifoBuffer<std::byte>::MoveFrom(src);
	}

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
