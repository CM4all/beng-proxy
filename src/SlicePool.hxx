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
#include "util/Compiler.h"
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
	SlicePool(std::size_t _slice_size, unsigned _slices_per_area) noexcept;
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

	gcc_pure
	AllocatorStats GetStats() const noexcept;

	void Compress() noexcept;

	SliceAllocation Alloc() noexcept;
	void Free(SliceArea &area, void *p) noexcept;

private:
	gcc_pure
	SliceArea *FindNonFullArea() noexcept;

	SliceArea &MakeNonFullArea() noexcept;
};
