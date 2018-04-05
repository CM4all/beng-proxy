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

/*
 * The "slice" memory allocator.  It is an allocator for large numbers
 * of small fixed-size objects.
 */

#ifndef BENG_PROXY_SLICE_POOL_HXX
#define BENG_PROXY_SLICE_POOL_HXX

#include "util/Compiler.h"

#include <boost/intrusive/list.hpp>

#include <assert.h>
#include <stddef.h>

struct AllocatorStats;
class SlicePool;
class SliceArea;

struct SliceAllocation {
    SliceArea *area;

    void *data;
    size_t size;
};

class SliceArea {
public:
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;
    SiblingsHook siblings;

private:
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

    SliceArea(SlicePool &pool) noexcept;

    ~SliceArea() noexcept {
        assert(allocated_count == 0);
    }

public:
    static SliceArea *New(SlicePool &pool) noexcept;
    void Delete(SlicePool &pool) noexcept;

    static constexpr size_t GetHeaderSize(unsigned slices_per_area) noexcept {
        return sizeof(SliceArea) + sizeof(Slot) * (slices_per_area - 1);
    }

    void ForkCow(const SlicePool &pool, bool inherit) noexcept;

    bool IsEmpty() const noexcept {
        return allocated_count == 0;
    }

    bool IsFull(const SlicePool &pool) const noexcept;

    size_t GetNettoSize(size_t slice_size) const noexcept {
        return allocated_count * slice_size;
    }

    gcc_pure
    void *GetPage(const SlicePool &pool, unsigned page) noexcept;

    gcc_pure
    void *GetSlice(const SlicePool &pool, unsigned slice) noexcept;

    /**
     * Calculates the allocation slot index from an allocated pointer.
     * This is used to locate the #Slot for a pointer passed to a
     * public function.
     */
    gcc_pure
    unsigned IndexOf(const SlicePool &pool, const void *_p) noexcept;

    /**
     * Find the first free slot index, starting at the specified position.
     */
    gcc_pure
    unsigned FindFree(const SlicePool &pool, unsigned start) const noexcept;

    /**
     * Find the first allocated slot index, starting at the specified
     * position.
     */
    gcc_pure
    unsigned FindAllocated(const SlicePool &pool,
                           unsigned start) const noexcept;

    /**
     * Punch a hole in the memory map in the specified slot index range.
     * This means notifying the kernel that we will no longer need the
     * contents, which allows the kernel to drop the allocated pages and
     * reuse it for other processes.
     */
    void PunchSliceRange(SlicePool &pool,
                         unsigned start, unsigned end) noexcept;

    void Compress(SlicePool &pool) noexcept;

    void *Alloc(SlicePool &pool) noexcept;
    void Free(SlicePool &pool, void *p) noexcept;

    struct Disposer {
        SlicePool &pool;

        void operator()(SliceArea *area) noexcept {
            area->Delete(pool);
        }
    };
};

class SlicePool {
    friend class SliceArea;

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

public:
    SlicePool(size_t _slice_size, unsigned _slices_per_area) noexcept;
    ~SlicePool() noexcept;

    size_t GetSliceSize() const noexcept {
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

    gcc_pure
    SliceArea *FindNonFullArea() noexcept;

    SliceArea &MakeNonFullArea() noexcept;

    SliceAllocation Alloc() noexcept;
    void Free(SliceArea &area, void *p) noexcept;
};

#endif
