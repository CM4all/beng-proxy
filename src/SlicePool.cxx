/*
 * Copyright 2007-2017 Content Management AG
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

#include "SlicePool.hxx"
#include "SliceArea.hxx"
#include "system/mmap.h"
#include "AllocatorStats.hxx"
#include "util/Poison.h"

#include <new>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

constexpr
static inline size_t
align_size(size_t size) noexcept
{
    return ((size - 1) | 0x1f) + 1;
}

gcc_const
static inline size_t
align_page_size(size_t size) noexcept
{
    return ((size - 1) | (mmap_page_size() - 1)) + 1;
}

static constexpr unsigned
divide_round_up(unsigned a, unsigned b) noexcept
{
    return (a + b - 1) / b;
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
                       mmap_page_size() * (pool.pages_per_area - pool.header_pages));
}

SliceArea *
SliceArea::New(SlicePool &pool) noexcept
{
    void *p = mmap_alloc_anonymous(pool.area_size);
    if (p == (void *)-1) {
        fputs("Out of adress space\n", stderr);
        abort();
    }

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
    mmap_free(this, pool.area_size);
}

inline void *
SliceArea::GetPage(unsigned page) noexcept
{
    assert(page <= pool.pages_per_area);

    return (uint8_t *)this + (pool.header_pages + page) * mmap_page_size();
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

    size_t offset = p - (const uint8_t *)this;
    const unsigned page = offset / mmap_page_size() - pool.header_pages;
    offset %= mmap_page_size();
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
gcc_pure
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
SliceArea::PunchSliceRange(unsigned start, gcc_unused unsigned end) noexcept
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

    mmap_discard_pages(start_pointer, end_pointer - start_pointer);
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

SlicePool::SlicePool(size_t _slice_size, unsigned _slices_per_area) noexcept
{
    assert(_slice_size > 0);
    assert(_slices_per_area > 0);

    if (_slice_size <= mmap_page_size() / 2) {
        slice_size = align_size(_slice_size);

        slices_per_page = mmap_page_size() / slice_size;
        pages_per_slice = 1;

        pages_per_area = divide_round_up(_slices_per_area,
                                         slices_per_page);
    } else {
        slice_size = align_page_size(_slice_size);

        slices_per_page = 1;
        pages_per_slice = slice_size / mmap_page_size();

        pages_per_area = _slices_per_area * pages_per_slice;
    }

    slices_per_area = (pages_per_area / pages_per_slice) * slices_per_page;

    const size_t header_size = SliceArea::GetHeaderSize(slices_per_area);
    header_pages = divide_round_up(header_size, mmap_page_size());

    area_size = mmap_page_size() * (header_pages + pages_per_area);
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
    mmap_enable_fork(this, pool.area_size, inherit);
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

gcc_pure
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
    auto &area = MakeNonFullArea();

    const bool was_empty = area.IsEmpty();

    void *p = area.Alloc();

    if (area.IsFull()) {
        /* if the area has become full, move it to the back of the
           linked list, to avoid iterating over a long list of full
           areas in the next call */
        areas.erase(areas.iterator_to(area));
        full_areas.push_back(area);
    } else if (was_empty) {
        empty_areas.erase(empty_areas.iterator_to(area));
        areas.push_back(area);
    }

    return { &area, p, slice_size };
}

inline void
SliceArea::Free(void *p) noexcept
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
    const bool was_full = area.IsFull();

    area.Free(p);

    if (was_full) {
        /* if the area has become non-full, move it to the front of
           the linked list, so the next allocation will be taken from
           here; this attempts to keep as many areas as possible
           completely empty, so the next Compress() call can dispose
           them */
        full_areas.erase(full_areas.iterator_to(area));
        areas.push_front(area);
    } else if (area.IsEmpty()) {
        areas.erase(areas.iterator_to(area));
        empty_areas.push_front(area);
    }
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
