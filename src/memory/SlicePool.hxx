// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "SliceAllocation.hxx"
#include "util/IntrusiveList.hxx"

#include <cstddef>

struct AllocatorStats;
class SliceArea;

/**
 * The "slice" memory allocator.  It is an allocator for large numbers
 * of small fixed-size objects.
 */
class SlicePool {
	friend class SliceArea;

	const char *const vma_name;

	std::size_t slice_size;

	/**
	 * Number of slices that fit on one MMU page (4 kB).
	 */
	unsigned slices_per_page;

	unsigned pages_per_slice;

	unsigned pages_per_area;

	unsigned slices_per_area;

	/**
	 * Number of pages for the area header.
	 */
	unsigned header_pages;

	std::size_t area_size;

	using AreaList = IntrusiveList<SliceArea>;

	AreaList areas;

	/**
	 * A list of #SliceArea instances which are empty.  They are kept
	 * in a separate list to reduce fragmentation: allocate first from
	 * areas which are not empty.
	 */
	AreaList empty_areas;

	/**
	 * A list of #SliceArea instances which are full.  They are kept
	 * in a separate list to speed up allocation, to avoid iterating
	 * over full areas.
	 */
	AreaList full_areas;

	bool fork_cow = true;

public:
	SlicePool(std::size_t _slice_size, unsigned _slices_per_area,
		  const char *_vma_name) noexcept;
	~SlicePool() noexcept;

	std::size_t GetSliceSize() const noexcept {
		return slice_size;
	}

	/**
	 * Controls whether forked child processes inherit the allocator.
	 * This is enabled by default.
	 */
	void ForkCow(bool inherit) noexcept;

	void AddStats(AllocatorStats &stats, const AreaList &list) const noexcept;

	[[gnu::pure]]
	AllocatorStats GetStats() const noexcept;

	void Compress() noexcept;

	SliceAllocation Alloc() noexcept;
	void Free(SliceArea &area, void *p) noexcept;

private:
	[[gnu::pure]]
	SliceArea *FindNonFullArea() noexcept;

	SliceArea &MakeNonFullArea() noexcept;
};
