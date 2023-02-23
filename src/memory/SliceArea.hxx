// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/IntrusiveList.hxx"

#include <cassert>
#include <cstddef>

class SlicePool;

/**
 * @see #SlicePool
 */
class SliceArea
	: public IntrusiveListHook<IntrusiveHookMode::NORMAL>
{
	SlicePool &pool;

	unsigned allocated_count = 0;

	unsigned free_head = 0;

	struct Slot {
		unsigned next;

		static constexpr unsigned ALLOCATED = -1;
		static constexpr unsigned END_OF_LIST = -2;

#ifndef NDEBUG
		static constexpr unsigned MARK = -3;
#endif

		constexpr bool IsAllocated() const noexcept {
			return next == ALLOCATED;
		}
	};

	Slot slices[1];

	explicit SliceArea(SlicePool &pool) noexcept;

	~SliceArea() noexcept {
		assert(allocated_count == 0);
	}

public:
	static SliceArea *New(SlicePool &pool) noexcept;
	void Delete() noexcept;

	static constexpr std::size_t GetHeaderSize(unsigned slices_per_area) noexcept {
		return sizeof(SliceArea) + sizeof(Slot) * (slices_per_area - 1);
	}

	void ForkCow(bool inherit) noexcept;

	bool IsEmpty() const noexcept {
		return allocated_count == 0;
	}

	bool IsFull() const noexcept;

	std::size_t GetNettoSize(std::size_t slice_size) const noexcept {
		return allocated_count * slice_size;
	}

	[[gnu::pure]]
	void *GetPage(unsigned page) noexcept;

	[[gnu::pure]]
	void *GetSlice(unsigned slice) noexcept;

	/**
	 * Calculates the allocation slot index from an allocated pointer.
	 * This is used to locate the #Slot for a pointer passed to a
	 * public function.
	 */
	[[gnu::pure]]
	unsigned IndexOf(const void *_p) noexcept;

	/**
	 * Find the first free slot index, starting at the specified position.
	 */
	[[gnu::pure]]
	unsigned FindFree(unsigned start) const noexcept;

	/**
	 * Find the first allocated slot index, starting at the specified
	 * position.
	 */
	[[gnu::pure]]
	unsigned FindAllocated(unsigned start) const noexcept;

	/**
	 * Punch a hole in the memory map in the specified slot index range.
	 * This means notifying the kernel that we will no longer need the
	 * contents, which allows the kernel to drop the allocated pages and
	 * reuse it for other processes.
	 */
	void PunchSliceRange(unsigned start, unsigned end) noexcept;

	void Compress() noexcept;

	void *Alloc() noexcept;

	/**
	 * Internal method only to be used by SlicePool::Free().
	 */
	void _Free(void *p) noexcept;

	void Free(void *p) noexcept;

	struct Disposer {
		void operator()(SliceArea *area) noexcept {
			area->Delete();
		}
	};
};
