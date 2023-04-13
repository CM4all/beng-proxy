// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SlicePool.hxx"
#include "SliceArea.hxx"
#include "stats/AllocatorStats.hxx"
#include "system/PageAllocator.hxx"
#include "system/HugePage.hxx"
#include "system/VmaName.hxx"
#include "util/Poison.h"
#include "util/Sanitizer.hxx"
#include "util/Valgrind.hxx"

#include <new>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

static constexpr std::size_t
align_size(std::size_t size) noexcept
{
	return RoundUpToPowerOfTwo(size, (std::size_t)0x20);
}

static constexpr unsigned
divide_round_up(unsigned a, unsigned b) noexcept
{
	return (a + b - 1) / b;
}

[[gnu::const]]
static bool
HaveMemoryChecker() noexcept
{
	if (HaveAddressSanitizer())
		return true;

	if (HaveValgrind())
		return true;

	return false;
}

/*
 * SliceArea methods
 *
 */

inline
SliceArea::SliceArea(SlicePool &_pool) noexcept
	:pool(_pool)
{
	/* build the "free" list */
	for (unsigned i = 0; i < pool.slices_per_area - 1; ++i)
		slices[i].next = i + 1;

	slices[pool.slices_per_area - 1].next = Slot::END_OF_LIST;

	PoisonInaccessible(GetPage(pool.header_pages),
			   PAGE_SIZE * (pool.pages_per_area - pool.header_pages));
}

SliceArea *
SliceArea::New(SlicePool &pool) noexcept
{
	void *p = AllocatePages(pool.area_size);

	if (pool.vma_name != nullptr)
		SetVmaName(p, pool.area_size, pool.vma_name);

	if (std::size_t huge_size = AlignHugePageDown(pool.area_size);
	    huge_size > 0)
		EnableHugePages(p, huge_size);

	return ::new(p) SliceArea(pool);
}

inline bool
SliceArea::IsFull() const noexcept
{
	assert(free_head < pool.slices_per_area ||
	       free_head == Slot::END_OF_LIST);

	return free_head == Slot::END_OF_LIST;
}

void
SliceArea::Delete() noexcept
{
	assert(allocated_count == 0);

#ifndef NDEBUG
	for (unsigned i = 0; i < pool.slices_per_area; ++i)
		assert(slices[i].next < pool.slices_per_area ||
		       slices[i].next == Slot::END_OF_LIST);

	unsigned i = free_head;
	while (i != Slot::END_OF_LIST) {
		assert(i < pool.slices_per_area);

		unsigned next = slices[i].next;
		slices[i].next = Slot::MARK;
		i = next;
	}
#endif

	this->~SliceArea();
	FreePages(this, pool.area_size);
}

inline void *
SliceArea::GetPage(unsigned page) noexcept
{
	assert(page <= pool.pages_per_area);

	return (uint8_t *)this + (pool.header_pages + page) * PAGE_SIZE;
}

inline void *
SliceArea::GetSlice(unsigned slice) noexcept
{
	assert(slice < pool.slices_per_area);
	assert(slices[slice].IsAllocated());

	unsigned page = (slice / pool.slices_per_page) * pool.pages_per_slice;
	slice %= pool.slices_per_page;

	return (uint8_t *)GetPage(page) + slice * pool.slice_size;
}

inline unsigned
SliceArea::IndexOf(const void *_p) noexcept
{
	const uint8_t *p = (const uint8_t *)_p;
	assert(p >= (uint8_t *)GetPage(0));
	assert(p < (uint8_t *)GetPage(pool.pages_per_area));

	std::size_t offset = p - (const uint8_t *)this;
	const unsigned page = offset / PAGE_SIZE - pool.header_pages;
	offset %= PAGE_SIZE;
	assert(offset % pool.slice_size == 0);

	return page * pool.slices_per_page / pool.pages_per_slice
		+ offset / pool.slice_size;
}

unsigned
SliceArea::FindFree(unsigned start) const noexcept
{
	assert(start <= pool.slices_per_page);

	const unsigned end = pool.slices_per_page;

	unsigned i;
	for (i = start; i != end; ++i)
		if (!slices[i].IsAllocated())
			break;

	return i;
}

/**
 * Find the first allocated slot index, starting at the specified
 * position.
 */
[[gnu::pure]]
unsigned
SliceArea::FindAllocated(unsigned start) const noexcept
{
	assert(start <= pool.slices_per_page);

	const unsigned end = pool.slices_per_page;

	unsigned i;
	for (i = start; i != end; ++i)
		if (slices[i].IsAllocated())
			break;

	return i;
}

void
SliceArea::PunchSliceRange(unsigned start,
			   [[maybe_unused]] unsigned end) noexcept
{
	assert(start <= end);

	unsigned start_page = divide_round_up(start, pool.slices_per_page)
		* pool.pages_per_slice;
	unsigned end_page = (start / pool.slices_per_page )
		* pool.pages_per_slice;
	assert(start_page <= end_page + 1);
	if (start_page >= end_page)
		return;

	uint8_t *start_pointer = (uint8_t *)GetPage(start_page);
	uint8_t *end_pointer = (uint8_t *)GetPage(end_page);

	DiscardPages(start_pointer, end_pointer - start_pointer);
}

void
SliceArea::Compress() noexcept
{
	unsigned position = 0;

	while (true) {
		unsigned first_free = FindFree(position);
		if (first_free == pool.slices_per_page)
			break;

		unsigned first_allocated = FindAllocated(first_free + 1);
		PunchSliceRange(first_free, first_allocated);

		position = first_allocated;
	}
}

/*
 * SlicePool methods
 *
 */

SlicePool::SlicePool(std::size_t _slice_size, unsigned _slices_per_area,
		     const char *_vma_name) noexcept
	:vma_name(_vma_name)
{
	assert(_slice_size > 0);
	assert(_slices_per_area > 0);

	if (_slice_size <= PAGE_SIZE / 2) {
		slice_size = align_size(_slice_size);

		slices_per_page = PAGE_SIZE / slice_size;
		pages_per_slice = 1;

		pages_per_area = divide_round_up(_slices_per_area,
						 slices_per_page);
	} else {
		slice_size = AlignToPageSize(_slice_size);

		slices_per_page = 1;
		pages_per_slice = slice_size / PAGE_SIZE;

		pages_per_area = _slices_per_area * pages_per_slice;
	}

	slices_per_area = (pages_per_area / pages_per_slice) * slices_per_page;

	const std::size_t header_size = SliceArea::GetHeaderSize(slices_per_area);
	header_pages = divide_round_up(header_size, PAGE_SIZE);

	area_size = PAGE_SIZE * (header_pages + pages_per_area);
}

SlicePool::~SlicePool() noexcept
{
	assert(areas.empty());
	assert(full_areas.empty());

	empty_areas.clear_and_dispose(SliceArea::Disposer());
}

void
SliceArea::ForkCow(bool inherit) noexcept
{
	EnablePageFork(this, pool.area_size, inherit);
}

void
SlicePool::ForkCow(bool inherit) noexcept
{
	if (inherit == fork_cow)
		return;

	fork_cow = inherit;
	for (auto &area : areas)
		area.ForkCow(fork_cow);

	for (auto &area : empty_areas)
		area.ForkCow(fork_cow);

	for (auto &area : full_areas)
		area.ForkCow(fork_cow);
}

void
SlicePool::Compress() noexcept
{
	for (auto &area : areas)
		area.Compress();

	empty_areas.clear_and_dispose(SliceArea::Disposer());

	/* compressing full_areas would have no effect */
}

[[gnu::pure]]
inline SliceArea *
SlicePool::FindNonFullArea() noexcept
{
	if (!areas.empty())
		return &areas.front();

	if (!empty_areas.empty())
		return &empty_areas.front();

	return nullptr;
}

inline SliceArea &
SlicePool::MakeNonFullArea() noexcept
{
	SliceArea *area = FindNonFullArea();
	if (area == nullptr) {
		area = SliceArea::New(*this);
		area->ForkCow(fork_cow);
		empty_areas.push_front(*area);
	}

	return *area;
}

inline void *
SliceArea::Alloc() noexcept
{
	assert(!IsFull());

	const unsigned i = free_head;
	auto *const slot = &slices[i];

	++allocated_count;
	free_head = slot->next;
	slot->next = Slot::ALLOCATED;

	auto *p = GetSlice(i);
	PoisonUndefined(p, pool.slice_size);
	return p;
}

SliceAllocation
SlicePool::Alloc() noexcept
{
	if (HaveMemoryChecker())
		return SliceAllocation{ malloc(slice_size), slice_size };

	auto &area = MakeNonFullArea();

	const bool was_empty = area.IsEmpty();

	void *p = area.Alloc();

	if (area.IsFull()) {
		/* if the area has become full, move it to the back of the
		   linked list, to avoid iterating over a long list of full
		   areas in the next call */
		area.unlink();
		full_areas.push_back(area);
	} else if (was_empty) {
		area.unlink();
		areas.push_back(area);
	}

	return { area, p, slice_size };
}

inline void
SliceArea::_Free(void *p) noexcept
{
	unsigned i = IndexOf(p);
	assert(slices[i].IsAllocated());

	PoisonUndefined(p, pool.slice_size);

	slices[i].next = free_head;
	free_head = i;

	assert(allocated_count > 0);
	--allocated_count;
}

void
SlicePool::Free(SliceArea &area, void *p) noexcept
{
	if (HaveMemoryChecker()) {
		free(p);
		return;
	}

	const bool was_full = area.IsFull();

	area._Free(p);

	if (was_full) {
		/* if the area has become non-full, move it to the front of
		   the linked list, so the next allocation will be taken from
		   here; this attempts to keep as many areas as possible
		   completely empty, so the next Compress() call can dispose
		   them */
		area.unlink();
		areas.push_front(area);
	} else if (area.IsEmpty()) {
		area.unlink();
		empty_areas.push_front(area);
	}
}

void
SliceArea::Free(void *p) noexcept
{
	if (HaveMemoryChecker()) {
		free(p);
		return;
	}

	pool.Free(*this, p);
}

inline void
SlicePool::AddStats(AllocatorStats &stats, const AreaList &list) const noexcept
{
	for (const auto &area : list) {
		stats.brutto_size += area_size;
		stats.netto_size += area.GetNettoSize(slice_size);
	}
}

AllocatorStats
SlicePool::GetStats() const noexcept
{
	AllocatorStats stats;
	stats.brutto_size = stats.netto_size = 0;

	AddStats(stats, areas);
	AddStats(stats, empty_areas);
	AddStats(stats, full_areas);

	return stats;
}
