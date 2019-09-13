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

#ifndef BENG_PROXY_RUBBER_HXX
#define BENG_PROXY_RUBBER_HXX

#include "system/LargeObject.hxx"
#include "util/Compiler.h"

#include <boost/intrusive/list.hpp>

#include <array>
#include <algorithm>

#include <assert.h>
#include <stddef.h>

struct AllocatorStats;
struct RubberObject;
struct RubberTable;

/**
 * The "rubber" memory allocator.  It is a buffer for storing many
 * large objects.  Unlike heap memory, unused areas are given back to
 * the operating system.
 */
class Rubber {
    /**
     * The sum of all allocation sizes.
     */
    size_t netto_size = 0;

    /**
     * The table managing the allocations in the memory map.  At the
     * same time, this is the pointer to the memory map.
     */
    LargeObject<RubberTable> table;

    /**
     * The threshold for each hole list.  The goal is to reduce the cost
     * of searching a hole that fits.
     */
    static constexpr size_t HOLE_THRESHOLDS[] = {
        1024 * 1024, 64 * 1024, 32 * 1024, 16 * 1024, 8192, 4096, 2048, 1024, 64, 0
    };

    gcc_const
    static unsigned LookupHoleThreshold(size_t size) noexcept {
        for (unsigned i = 0;; ++i)
            if (size >= HOLE_THRESHOLDS[i])
                return i;
    }

    static constexpr size_t N_HOLE_THRESHOLDS = std::size(HOLE_THRESHOLDS);

    struct Hole final
        : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

        /**
         * The size of this hole (including the size of this struct).
         */
        size_t size;

        /**
         * The allocated objects before and after this hole.
         */
        unsigned previous_id, next_id;
    };

    typedef boost::intrusive::list<Hole,
                                   boost::intrusive::constant_time_size<false>> HoleList;

    /**
     * A list of all holes in the buffer.  Each array element hosts
     * its own list with holes at the size of
     * HOLE_THRESHOLDS[i] or bigger.
     */
    std::array<HoleList, N_HOLE_THRESHOLDS> holes;

public:
    /**
     * Throws std::bad_alloc on error.
     */
    explicit Rubber(size_t _max_size);

    ~Rubber() noexcept;

    /**
     * Controls whether forked child processes inherit the allocator.
     * This is enabled by default.
     */
    void ForkCow(bool inherit) noexcept;

    /**
     * Returns the maximum total size of all allocations.
     */
    gcc_pure
    size_t GetMaxSize() const noexcept;

    /**
     * Returns the total size of all allocations.
     */
    size_t GetNettoSize() const noexcept {
        return netto_size;
    }

    /**
     * Returns the memory consumed by this object, not including the
     * allocation table.
     */
    gcc_pure
    size_t GetBruttoSize() const noexcept;

    gcc_pure
    AllocatorStats GetStats() const noexcept;

    void Compress() noexcept;

    /**
     * Add a new object with the specified size.  Use Write() to
     * actually copy data to the object.
     *
     * @param size the size, must be positive
     * @return the object id, or 0 on error
     */
    unsigned Add(size_t size) noexcept;
    void Remove(unsigned id) noexcept;

    /**
     * Shrink an object.  The new size must be smaller (or equal) to
     * the current size.  This is done in-place, possibly leaving a
     * gap that can only be used again after Compress() has been
     * called.
     *
     * @param new_size the new size, must be positive
     */
    void Shrink(unsigned id, size_t new_size) noexcept;

    /**
     * Returns the size of an allocation.  Due to padding, the
     * returned value may be a bit bigger than the size that was
     * passed to Add().
     */
    gcc_pure
    size_t GetSizeOf(unsigned id) const noexcept;

    /**
     * Return a writable pointer to the object.
     */
    gcc_pure
    void *Write(unsigned id) noexcept;

    /**
     * Return a read-only pointer to the object.
     */
    gcc_pure
    const void *Read(unsigned id) const noexcept;

private:
    gcc_pure
    void *WriteAt(size_t offset) noexcept {
        assert(offset <= table.size());

        return (uint8_t *)table.get() + offset;
    }

    gcc_pure
    const void *ReadAt(size_t offset) const noexcept {
        assert(offset <= table.size());

        return (const uint8_t *)table.get() + offset;
    }

    size_t OffsetOf(const void *p) const noexcept {
        return (const uint8_t *)p - (const uint8_t *)table.get();
    }

    size_t OffsetOf(const Hole &hole) const noexcept {
        return OffsetOf(&hole);
    }

    gcc_pure
    static size_t GetTotalHoleSize(const HoleList &holes) noexcept;

#ifndef NDEBUG
    size_t GetTotalHoleSize() const noexcept;
#endif

    gcc_pure
    static Hole *FindHole(HoleList &holes, size_t size) noexcept;

    gcc_pure
    Hole *FindHole(size_t size) noexcept;

    void AddToHoleList(Hole &hole) noexcept;

    void AddHole(size_t offset, size_t size,
                 unsigned previous_id, unsigned next_id) noexcept;
    void AddHoleAfter(unsigned reference_id,
                      size_t offset, size_t size) noexcept;

    /**
     * Replace the hole with the specified object.  If there is unused
     * space after the object, create a new #Hole instance
     * there.
     */
    void UseHole(Hole &hole, unsigned id, size_t size) noexcept;

    unsigned AddInHole(Hole &hole, size_t size) noexcept;

    /**
     * Try to find a hole between two objects, and insert a new object
     * there.
     *
     * @return the object id, or 0 on error
     */
    unsigned AddInHole(size_t size) noexcept;

    /**
     * Attempt to move the last allocation into a hole.  This is some kind
     * of simplified defragmentation.  It attempts to keep the "brutto"
     * size of this allocator small by filling holes.
     *
     * @param max_object_size move it only if it's not larger than this size
     */
    bool MoveLast(size_t max_object_size) noexcept;

    gcc_pure
    Hole *FindHoleBetween(RubberObject &a, RubberObject &b) noexcept;

    /**
     * If there is a hole between the two objects, discard it.  This
     * is used to remove holes at the end of the mmap when the last
     * object got removed.
     */
    void DiscardHoleBetween(RubberObject &a, RubberObject &b) noexcept;

    /**
     * The given object shall disappear at its current offset.  This
     * method will replace it with a #Hole instance, or will
     * grow/merge existing #Hole instances surrounding it.
     *
     * This method will not remove the #RubberObject from the table /
     * linked list, nor will it update the netto size.  It assumes
     * that the #RubberObject has already been removed from the linked
     * list.  It will corrupt data previously allocated by the
     * #RubberObject.
     */
    void ReplaceWithHole(RubberObject &o,
                         unsigned previous_id, unsigned next_id) noexcept;

    void MoveData(RubberObject &o, size_t new_offset) noexcept;

    HoleList &GetHoleList(size_t size) noexcept {
        return holes[LookupHoleThreshold(size)];
    }

    HoleList &GetHoleList(Hole &hole) noexcept {
        return GetHoleList(hole.size);
    }

    void RemoveHole(Hole &hole) noexcept {
        GetHoleList(hole).erase(HoleList::s_iterator_to(hole));
    }
};

/**
 * An allocation from a #Rubber instance.  This class "owns" the
 * allocation and frees it automatically.
 */
class RubberAllocation {
    Rubber *rubber = nullptr;
    unsigned id = 0;

public:
    RubberAllocation() = default;

    RubberAllocation(Rubber &_rubber, unsigned _id) noexcept
        :rubber(&_rubber), id(_id) {}

    RubberAllocation(RubberAllocation &&src) noexcept
        :rubber(std::exchange(src.rubber, nullptr)),
         id(std::exchange(src.id, 0)) {}

    ~RubberAllocation() {
        if (id != 0)
            rubber->Remove(id);
    }

    RubberAllocation &operator=(RubberAllocation &&src) noexcept {
        using std::swap;
        swap(rubber, src.rubber);
        swap(id, src.id);
        return *this;
    }

    operator bool() const noexcept {
        return id != 0;
    }

    Rubber &GetRubber() const noexcept {
        assert(*this);
        return *rubber;
    }

    unsigned GetId() const noexcept {
        assert(*this);
        return id;
    }

    void Shrink(size_t new_size) noexcept {
        assert(*this);

        rubber->Shrink(id, new_size);
    }

    void *Write() noexcept {
        assert(*this);

        return rubber->Write(id);
    }

    const void *Read() noexcept {
        assert(*this);

        return rubber->Read(id);
    }
};

#endif
