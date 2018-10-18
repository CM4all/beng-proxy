/*
 * Copyright 2007-2018 Content Management AG
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

#include "util/Compiler.h"

#include <boost/intrusive/list_hook.hpp>

#include <assert.h>
#include <stddef.h>

class SlicePool;

/**
 * @see #SlicePool
 */
class SliceArea
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

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

    static constexpr size_t GetHeaderSize(unsigned slices_per_area) noexcept {
        return sizeof(SliceArea) + sizeof(Slot) * (slices_per_area - 1);
    }

    void ForkCow(bool inherit) noexcept;

    bool IsEmpty() const noexcept {
        return allocated_count == 0;
    }

    bool IsFull() const noexcept;

    size_t GetNettoSize(size_t slice_size) const noexcept {
        return allocated_count * slice_size;
    }

    gcc_pure
    void *GetPage(unsigned page) noexcept;

    gcc_pure
    void *GetSlice(unsigned slice) noexcept;

    /**
     * Calculates the allocation slot index from an allocated pointer.
     * This is used to locate the #Slot for a pointer passed to a
     * public function.
     */
    gcc_pure
    unsigned IndexOf(const void *_p) noexcept;

    /**
     * Find the first free slot index, starting at the specified position.
     */
    gcc_pure
    unsigned FindFree(unsigned start) const noexcept;

    /**
     * Find the first allocated slot index, starting at the specified
     * position.
     */
    gcc_pure
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
