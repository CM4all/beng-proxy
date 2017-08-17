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
#include "system/mmap.h"
#include "AllocatorStats.hxx"

#include <boost/intrusive/list.hpp>

#include <new>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

static constexpr unsigned ALLOCATED = -1;
static constexpr unsigned END_OF_LIST = -2;

#ifndef NDEBUG
static constexpr unsigned MARK = -3;
#endif

struct SliceSlot {
    unsigned next;

    constexpr bool IsAllocated() const {
        return next == ALLOCATED;
    }
};

struct SliceArea {
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;
    SiblingsHook siblings;

    unsigned allocated_count;

    unsigned free_head;

    SliceSlot slices[1];

private:
    SliceArea(SlicePool &pool);

    ~SliceArea() {
        assert(allocated_count == 0);
    }

public:
    static SliceArea *New(SlicePool &pool);
    void Delete(SlicePool &pool);

    void ForkCow(const SlicePool &pool, bool inherit);

    bool IsEmpty() const {
        return allocated_count == 0;
    }

    bool IsFull(const SlicePool &pool) const;

    size_t GetNettoSize(size_t slice_size) const {
        return allocated_count * slice_size;
    }

    gcc_pure
    void *GetPage(const SlicePool &pool, unsigned page);

    gcc_pure
    void *GetSlice(const SlicePool &pool, unsigned slice);

    /**
     * Calculates the allocation slot index from an allocated pointer.
     * This is used to locate the #SliceSlot for a pointer passed to a
     * public function.
     */
    gcc_pure
    unsigned IndexOf(const SlicePool &pool, const void *_p);

    /**
     * Find the first free slot index, starting at the specified position.
     */
    gcc_pure
    unsigned FindFree(const SlicePool &pool, unsigned start) const;

    /**
     * Find the first allocated slot index, starting at the specified
     * position.
     */
    gcc_pure
    unsigned FindAllocated(const SlicePool &pool,
                           unsigned start) const;

    /**
     * Punch a hole in the memory map in the specified slot index range.
     * This means notifying the kernel that we will no longer need the
     * contents, which allows the kernel to drop the allocated pages and
     * reuse it for other processes.
     */
    void PunchSliceRange(SlicePool &pool,
                         unsigned start, gcc_unused unsigned end);

    void Compress(SlicePool &pool);

    void *Alloc(SlicePool &pool);
    void Free(SlicePool &pool, void *p);

    struct Disposer {
        SlicePool &pool;

        void operator()(SliceArea *area) {
            area->Delete(pool);
        }
    };
};

constexpr
static inline size_t
align_size(size_t size)
{
    return ((size - 1) | 0x1f) + 1;
}

gcc_const
static inline size_t
align_page_size(size_t size)
{
    return ((size - 1) | (mmap_page_size() - 1)) + 1;
}

static constexpr unsigned
divide_round_up(unsigned a, unsigned b)
{
    return (a + b - 1) / b;
}

struct SlicePool {
    size_t slice_size;

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

    size_t area_size;

    typedef boost::intrusive::list<SliceArea,
                                   boost::intrusive::member_hook<SliceArea,
                                                                 SliceArea::SiblingsHook,
                                                                 &SliceArea::siblings>,
                                   boost::intrusive::constant_time_size<false>> AreaList;

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

    SlicePool(size_t _slice_size, unsigned _slices_per_area);
    ~SlicePool();

    void ForkCow(bool inherit);

    void AddStats(AllocatorStats &stats, const AreaList &list) const;

    gcc_pure
    AllocatorStats GetStats() const;

    void Compress();

    gcc_pure
    SliceArea *FindNonFullArea();

    SliceArea &MakeNonFullArea();

    SliceAllocation Alloc();
    void Free(SliceArea &area, void *p);
};

/*
 * SliceArea methods
 *
 */

SliceArea::SliceArea(SlicePool &pool)
    :allocated_count(0), free_head(0)
{
    /* build the "free" list */
    for (unsigned i = 0; i < pool.slices_per_area - 1; ++i)
        slices[i].next = i + 1;

    slices[pool.slices_per_area - 1].next = END_OF_LIST;
}

SliceArea *
SliceArea::New(SlicePool &pool)
{
    void *p = mmap_alloc_anonymous(pool.area_size);
    if (p == (void *)-1) {
        fputs("Out of adress space\n", stderr);
        abort();
    }

    return ::new(p) SliceArea(pool);
}

inline bool
SliceArea::IsFull(gcc_unused const SlicePool &pool) const
{
    assert(free_head < pool.slices_per_area ||
           free_head == END_OF_LIST);

    return free_head == END_OF_LIST;
}

void
SliceArea::Delete(SlicePool &pool)
{
    assert(allocated_count == 0);

#ifndef NDEBUG
    for (unsigned i = 0; i < pool.slices_per_area; ++i)
        assert(slices[i].next < pool.slices_per_area ||
               slices[i].next == END_OF_LIST);

    unsigned i = free_head;
    while (i != END_OF_LIST) {
        assert(i < pool.slices_per_area);

        unsigned next = slices[i].next;
        slices[i].next = MARK;
        i = next;
    }
#endif

    this->~SliceArea();
    mmap_free(this, pool.area_size);
}

inline void *
SliceArea::GetPage(const SlicePool &pool, unsigned page)
{
    assert(page <= pool.pages_per_area);

    return (uint8_t *)this + (pool.header_pages + page) * mmap_page_size();
}

inline void *
SliceArea::GetSlice(const SlicePool &pool, unsigned slice)
{
    assert(slice < pool.slices_per_area);
    assert(slices[slice].IsAllocated());

    unsigned page = (slice / pool.slices_per_page) * pool.pages_per_slice;
    slice %= pool.slices_per_page;

    return (uint8_t *)GetPage(pool, page) + slice * pool.slice_size;
}

inline unsigned
SliceArea::IndexOf(const SlicePool &pool, const void *_p)
{
    const uint8_t *p = (const uint8_t *)_p;
    assert(p >= (uint8_t *)GetPage(pool, 0));
    assert(p < (uint8_t *)GetPage(pool, pool.pages_per_area));

    size_t offset = p - (const uint8_t *)this;
    const unsigned page = offset / mmap_page_size() - pool.header_pages;
    offset %= mmap_page_size();
    assert(offset % pool.slice_size == 0);

    return page * pool.slices_per_page / pool.pages_per_slice
        + offset / pool.slice_size;
}

unsigned
SliceArea::FindFree(const SlicePool &pool, unsigned start) const
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
SliceArea::FindAllocated(const SlicePool &pool, unsigned start) const
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
SliceArea::PunchSliceRange(SlicePool &pool,
                            unsigned start, gcc_unused unsigned end)
{
    assert(start <= end);

    unsigned start_page = divide_round_up(start, pool.slices_per_page)
        * pool.pages_per_slice;
    unsigned end_page = (start / pool.slices_per_page )
        * pool.pages_per_slice;
    assert(start_page <= end_page + 1);
    if (start_page >= end_page)
        return;

    uint8_t *start_pointer = (uint8_t *)GetPage(pool, start_page);
    uint8_t *end_pointer = (uint8_t *)GetPage(pool, end_page);

    mmap_discard_pages(start_pointer, end_pointer - start_pointer);
}

void
SliceArea::Compress(SlicePool &pool)
{
    unsigned position = 0;

    while (true) {
        unsigned first_free = FindFree(pool, position);
        if (first_free == pool.slices_per_page)
            break;

        unsigned first_allocated = FindAllocated(pool, first_free + 1);
        PunchSliceRange(pool, first_free, first_allocated);

        position = first_allocated;
    }
}

/*
 * SlicePool methods
 *
 */

inline
SlicePool::SlicePool(size_t _slice_size, unsigned _slices_per_area)
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

    const SliceArea *area = nullptr;
    const size_t header_size = sizeof(*area)
        + sizeof(area->slices[0]) * (slices_per_area - 1);
    header_pages = divide_round_up(header_size, mmap_page_size());

    area_size = mmap_page_size() * (header_pages + pages_per_area);
}

inline
SlicePool::~SlicePool()
{
    assert(areas.empty());
    assert(full_areas.empty());

    empty_areas.clear_and_dispose(SliceArea::Disposer{*this});
}

SlicePool *
slice_pool_new(size_t slice_size, unsigned slices_per_area)
{
    return new SlicePool(slice_size, slices_per_area);
}

void
slice_pool_free(SlicePool *pool)
{
    delete pool;
}

inline void
SliceArea::ForkCow(const SlicePool &pool, bool inherit)
{
    mmap_enable_fork(this, pool.area_size, inherit);
}

inline void
SlicePool::ForkCow(bool inherit)
{
    if (inherit == fork_cow)
        return;

    fork_cow = inherit;
    for (auto &area : areas)
        area.ForkCow(*this, fork_cow);

    for (auto &area : empty_areas)
        area.ForkCow(*this, fork_cow);

    for (auto &area : full_areas)
        area.ForkCow(*this, fork_cow);
}

void
slice_pool_fork_cow(SlicePool &pool, bool inherit)
{
    pool.ForkCow(inherit);
}

size_t
slice_pool_get_slice_size(const SlicePool *pool)
{
    return pool->slice_size;
}

inline void
SlicePool::Compress()
{
    for (auto &area : areas)
        area.Compress(*this);

    empty_areas.clear_and_dispose(SliceArea::Disposer{*this});

    /* compressing full_areas would have no effect */
}

void
slice_pool_compress(SlicePool *pool)
{
    pool->Compress();
}

gcc_pure
inline SliceArea *
SlicePool::FindNonFullArea()
{
    if (!areas.empty())
        return &areas.front();

    if (!empty_areas.empty())
        return &empty_areas.front();

    return nullptr;
}

inline SliceArea &
SlicePool::MakeNonFullArea()
{
    SliceArea *area = FindNonFullArea();
    if (area == nullptr) {
        area = SliceArea::New(*this);
        area->ForkCow(*this, fork_cow);
        empty_areas.push_front(*area);
    }

    return *area;
}

inline void *
SliceArea::Alloc(SlicePool &pool)
{
    assert(!IsFull(pool));

    const unsigned i = free_head;
    SliceSlot *const slot = &slices[i];

    ++allocated_count;
    free_head = slot->next;
    slot->next = ALLOCATED;

    return GetSlice(pool, i);
}

inline SliceAllocation
SlicePool::Alloc()
{
    auto &area = MakeNonFullArea();

    const bool was_empty = area.IsEmpty();

    void *p = area.Alloc(*this);

    if (area.IsFull(*this)) {
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

SliceAllocation
slice_alloc(SlicePool *pool)
{
    return pool->Alloc();
}

inline void
SliceArea::Free(SlicePool &pool, void *p)
{
    unsigned i = IndexOf(pool, p);
    assert(slices[i].IsAllocated());

    slices[i].next = free_head;
    free_head = i;

    assert(allocated_count > 0);
    --allocated_count;
}

inline void
SlicePool::Free(SliceArea &area, void *p)
{
    const bool was_full = area.IsFull(*this);

    area.Free(*this, p);

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

void
slice_free(SlicePool *pool, SliceArea *area, void *p)
{
    pool->Free(*area, p);
}

inline void
SlicePool::AddStats(AllocatorStats &stats, const AreaList &list) const
{
    for (const auto &area : list) {
        stats.brutto_size += area_size;
        stats.netto_size += area.GetNettoSize(slice_size);
    }
}

inline AllocatorStats
SlicePool::GetStats() const
{
    AllocatorStats stats;
    stats.brutto_size = stats.netto_size = 0;

    AddStats(stats, areas);
    AddStats(stats, empty_areas);
    AddStats(stats, full_areas);

    return stats;
}

AllocatorStats
slice_pool_get_stats(const SlicePool &pool)
{
    return pool.GetStats();
}
