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

#include "rubber.hxx"
#include "system/LargeAllocation.hxx"
#include "system/HugePage.hxx"
#include "system/mmap.h"
#include "AllocatorStats.hxx"
#include "util/Macros.hxx"

#include <boost/intrusive/list.hpp>

#include <array>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct RubberObject {
    /**
     * The next object index or 0 for end of list.
     */
    unsigned next;

    /**
     * The previous object index.  Not used for the "free" list.
     */
    unsigned previous;

    /**
     * The offset of this object within the memory map.
     */
    size_t offset;

    /**
     * The size of this object.
     */
    size_t size;

#ifndef NDEBUG
    bool allocated;
#endif

    void Init(size_t _offset, size_t _size) noexcept {
        offset = _offset;
        size = _size;
#ifndef NDEBUG
        allocated = true;
#endif
    }

    void InitHead(size_t _size) noexcept {
        next = previous = 0;
        offset = 0;
        size = _size;
    }

    constexpr size_t GetEndOffset() const noexcept {
        return offset + size;
    }
};

struct RubberTable {
    /**
     * The allocated size of the table (maximum number of objects).
     */
    unsigned max_entries;

    /**
     * The index after the last initialized table entry.  We avoid
     * initializing all entries on startup, because this may make the
     * kernel allocate physical memory for table areas we don't need
     * (yet).
     */
    unsigned initialized_tail;

    /**
     * The index of the first free table entry.  The linked list
     * contains all free entries in no specific order.  This is 0 if
     * the table is full.
     */
    unsigned free_head;

    /**
     * The first entry (index 0) is the table itself.  Its "previous"
     * attribute is the index of the allocated object with the largest
     * offset.
     */
    RubberObject entries[1];

    void Init(unsigned _max_entries) noexcept;
    void Deinit() noexcept;

    bool IsEmpty() const noexcept {
        return entries[0].next == 0;
    }

    unsigned IdOf(const RubberObject &o) const noexcept {
        assert(&o >= entries);
        assert(&o < &entries[max_entries]);

        return &o - entries;
    }

    /**
     * Calculate the size [in bytes] of a #RubberTable struct for the
     * given number of entries.
     */
    gcc_const
    static size_t RequiredSize(unsigned n) noexcept {
        assert(n > 0);

        const RubberTable *dummy = nullptr;
        return sizeof(*dummy) + sizeof(dummy->entries) * (n - 1);
    }

    /**
     * Calculate the capacity [in number of entries] of a #RubberTable
     * struct for the given size [in bytes].
     */
    gcc_const
    static unsigned Capacity(size_t size) noexcept {
        const RubberTable *dummy = nullptr;
        assert(size >= sizeof(*dummy));

        return (size - sizeof(*dummy)) / sizeof(dummy->entries) + 1;
    }

    /**
     * Returns the allocated size of the table object.  At the same time,
     * this is the offset of the first allocation.
     */
    gcc_pure
    size_t GetSize() const noexcept {
        assert(entries[0].offset == 0);

        return entries[0].size;
    }

    gcc_pure
    size_t GetBruttoSize() const noexcept {
        return GetTail().GetEndOffset() - GetSize();
    }

    RubberObject *GetHead() noexcept {
        return &entries[0];
    }

    RubberObject *GetNext(RubberObject *o) noexcept {
        return o->next != 0
            ? &entries[o->next]
            : nullptr;
    }

    gcc_const
    RubberObject &GetTail() noexcept {
        return entries[entries[0].previous];
    }

    gcc_const
    const RubberObject &GetTail() const noexcept {
        return entries[entries[0].previous];
    }

    gcc_pure
    size_t GetTailOffset() const noexcept {
        const auto &tail = GetTail();
        assert(tail.next == 0);

        return tail.GetEndOffset();
    }

    /**
     * Allocate a new object id.  The caller must initialise the object.
     */
    unsigned AddId() noexcept;

    /**
     * Insert an already-initialized object into the linked list.
     */
    void Link(unsigned id, unsigned previous_id, unsigned next_id) noexcept;

    unsigned Add(size_t offset, size_t size) noexcept;

    /**
     * Remove the object from the linked list.
     */
    void Unlink(unsigned id) noexcept;

    size_t Remove(unsigned id) noexcept;

    gcc_pure
    size_t GetSizeOf(unsigned id) const noexcept;

    gcc_pure
    size_t GetOffsetOf(unsigned id) const noexcept;

    size_t Shrink(unsigned id, size_t new_size) noexcept;
};

/**
 * The threshold for each hole list.  The goal is to reduce the cost
 * of searching a hole that fits.
 */
static constexpr size_t RUBBER_HOLE_THRESHOLDS[] = {
    1024 * 1024, 64 * 1024, 32 * 1024, 16 * 1024, 8192, 4096, 2048, 1024, 64, 0
};

gcc_pure
static unsigned
rubber_hole_threshold_lookup(size_t size) noexcept
{
    for (unsigned i = 0;; ++i)
        if (size >= RUBBER_HOLE_THRESHOLDS[i])
            return i;
}

static constexpr size_t N_RUBBER_HOLE_THRESHOLDS =
    ARRAY_SIZE(RUBBER_HOLE_THRESHOLDS);

class Rubber {
    /**
     * The maximum size of the memory map.  This is the value passed
     * to rubber_new() and will never be changed.
     */
    const size_t max_size;

    /**
     * The sum of all allocation sizes.
     */
    size_t netto_size;

    LargeAllocation allocation;

    /**
     * The table managing the allocations in the memory map.  At the
     * same time, this is the pointer to the memory map.
     */
    RubberTable *const table;

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
     * RUBBER_HOLE_THRESHOLDS[i] or bigger.
     */
    std::array<HoleList, N_RUBBER_HOLE_THRESHOLDS> holes;

public:
    explicit Rubber(size_t _max_size) noexcept;

    ~Rubber() noexcept {
        assert(table->IsEmpty());
        assert(netto_size == 0);

        table->Deinit();
        mmap_free(table, max_size);
    }

    void ForkCow(bool inherit) noexcept {
        mmap_enable_fork(table, max_size, inherit);
    }

    size_t GetMaxSize() const noexcept {
        return max_size - table->GetSize();
    }

    size_t GetNettoSize() const noexcept {
        return netto_size;
    }

    size_t GetBruttoSize() const noexcept {
        return table->GetBruttoSize();
    }

    void Compress() noexcept;

    unsigned Add(size_t size) noexcept;
    void Remove(unsigned id) noexcept;

    void Shrink(unsigned id, size_t new_size) noexcept;

    gcc_pure
    size_t GetSizeOf(unsigned id) const noexcept {
        assert(id > 0);

        return table->GetSizeOf(id);
    }

    gcc_pure
    void *Write(unsigned id) noexcept {
        const size_t offset = table->GetOffsetOf(id);
        assert(offset < max_size);
        return WriteAt(offset);
    }

    gcc_pure
    const void *Read(unsigned id) const noexcept {
        const size_t offset = table->GetOffsetOf(id);
        assert(offset < max_size);
        return ReadAt(offset);
    }

private:
    gcc_pure
    void *WriteAt(size_t offset) noexcept {
        assert(offset <= max_size);

        return (uint8_t *)table + offset;
    }

    gcc_pure
    const void *ReadAt(size_t offset) const noexcept {
        assert(offset <= max_size);

        return (const uint8_t *)table + offset;
    }

    size_t OffsetOf(const void *p) const noexcept {
        return (const uint8_t *)p - (const uint8_t *)table;
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
     * @param max_size move it only if it's not larger than this size
     */
    bool MoveLast(size_t max_object_size) noexcept;

    Hole *FindHoleBetween(RubberObject &a, RubberObject &b) noexcept {
        assert(a.offset < b.offset);

        return a.GetEndOffset() < b.offset
            ? (Hole *)WriteAt(a.GetEndOffset())
            : nullptr;
    }

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
        return holes[rubber_hole_threshold_lookup(size)];
    }

    HoleList &GetHoleList(Hole &hole) noexcept {
        return GetHoleList(hole.size);
    }

    void RemoveHole(Hole &hole) noexcept {
        GetHoleList(hole).erase(HoleList::s_iterator_to(hole));
    }
};

static const size_t RUBBER_ALIGN = 0x20;

gcc_const
static inline void *
align_page_size_ptr(void *p) noexcept
{
    return (void *)(long)AlignHugePageUp((size_t)p);
}

gcc_const
static inline size_t
align_size(size_t size) noexcept
{
    return ((size - 1) | (RUBBER_ALIGN - 1)) + 1;
}

/*
 * rubber_table
 *
 */

void
RubberTable::Init(unsigned _max_entries) noexcept
{
    assert(_max_entries > 1);

    initialized_tail = 1;

    uint8_t *const table_begin = (uint8_t *)this;

    /* round to nearest "huge page", so the first real allocation
       starts at a "huge page" boundary */
    uint8_t *const table_end = (uint8_t *)
        align_page_size_ptr(table_begin + RequiredSize(_max_entries));
    const size_t table_size = table_end - table_begin;

    entries[0].InitHead(table_size);

    max_entries = Capacity(table_size);

    free_head = 0;

#ifndef NDEBUG
    entries[0].allocated = true;
#endif
}

void
RubberTable::Deinit() noexcept
{
    assert(IsEmpty());
    assert(entries[0].next == 0);
    assert(entries[0].previous == 0);
    assert(entries[0].allocated);
}

/**
 * Allocate a new object id.  The caller must initialise the object.
 */
unsigned
RubberTable::AddId() noexcept
{
    if (free_head == 0) {
        if (initialized_tail >= max_entries)
            /* no more entries in the table (though there may still be
               enough space in the memory map) */
            return 0;

        return initialized_tail++;
    } else {

        /* remove the first item from the "free" list .. */

        unsigned id = free_head;
        auto &o = entries[id];
        assert(!o.allocated);
        free_head = o.next;
        return id;
    }
}

void
RubberTable::Link(unsigned id, unsigned previous_id, unsigned next_id) noexcept
{
    assert(id > 0);
    assert(id != previous_id);
    assert(id != next_id);

    auto &o = entries[id];
    assert(o.allocated);

    auto &previous = entries[previous_id];
    assert(previous.allocated);
    assert(previous.next == next_id);
    assert(previous.offset < o.offset);

    auto &next = entries[next_id];
    assert(previous.allocated);
    assert(next.previous == previous_id);
    assert(next_id == 0 || next.offset > o.offset);

    o.next = next_id;
    o.previous = previous_id;

    previous.next = id;
    next.previous = id;
}

unsigned
RubberTable::Add(size_t offset, size_t size) noexcept
{
    unsigned id = AddId();
    if (id == 0)
        return 0;

    auto &o = entries[id];
    o.Init(offset, size);

    /* .. and append it to the "allocated" list */

    Link(id, entries[0].previous, 0);

    /* done */

    return id;
}

size_t
RubberTable::GetSizeOf(unsigned id) const noexcept
{
    assert(entries[0].offset == 0);
    assert(entries[0].size >= sizeof(*this));
    assert(id > 0);
    assert(id < initialized_tail);
    assert(entries[id].allocated);

    return entries[id].size;
}

/**
 * @return the amount of memory that was freed
 */
size_t
RubberTable::Shrink(unsigned id, size_t new_size) noexcept
{
    assert(entries[0].offset == 0);
    assert(entries[0].size >= sizeof(*this));
    assert(id > 0);
    assert(id < initialized_tail);
    assert(entries[id].allocated);
    assert(entries[id].size >= new_size);

    size_t delta = entries[id].size - new_size;
    entries[id].size = new_size;

    return delta;
}

void
RubberTable::Unlink(unsigned id) noexcept
{
    assert(id > 0);
    assert(id < max_entries);

    auto &o = entries[id];
    assert(o.allocated);

    auto &next = entries[o.next];
    assert(next.allocated);
    assert(next.previous == id);
    assert(o.next == 0 || next.offset > o.offset);
    next.previous = o.previous;

    auto &previous = entries[o.previous];
    assert(previous.allocated);
    assert(previous.offset < o.offset);
    assert(previous.next == id);

    previous.next = o.next;
}

/**
 * @return the size of the allocation
 */
size_t
RubberTable::Remove(unsigned id) noexcept
{
    assert(GetSize() >= sizeof(*this));
    assert(id > 0);
    assert(id < max_entries);

    /* remove it from the "allocated" list */

    Unlink(id);

    /* add it to the "free" list */

    auto &o = entries[id];
    o.next = free_head;
    free_head = id;

#ifndef NDEBUG
    o.allocated = false;
#endif

    return o.size;
}

size_t
RubberTable::GetOffsetOf(unsigned id) const noexcept
{
    assert(GetSize() >= sizeof(*this));
    assert(id > 0);
    assert(id < max_entries);
    assert(id < initialized_tail);

    const auto &o = entries[id];
    assert(o.offset > 0);
    assert(o.offset >= GetSize());
    assert(entries[o.previous].offset < o.offset);
    assert(o.next == 0 || entries[o.next].offset > o.offset);
    assert(o.next == 0 || entries[o.next].offset >= o.GetEndOffset());

    return o.offset;
}

/*
 * rubber_hole list
 *
 */

#ifndef NDEBUG

inline size_t
Rubber::GetTotalHoleSize(const HoleList &holes) noexcept
{
    size_t result = 0;

    for (const auto &hole : holes) {
        assert(hole.size > 0);

        result += hole.size;
    }

    return result;
}

gcc_pure
size_t
Rubber::GetTotalHoleSize() const noexcept
{
    size_t result = 0;

    for (const auto &i : holes)
        result += GetTotalHoleSize(i);

    return result;
}

#endif

inline Rubber::Hole *
Rubber::FindHole(Rubber::HoleList &holes, size_t size) noexcept
{
    assert(size >= RUBBER_ALIGN);

    /* the current best candidate */
    Hole *best = nullptr;

    /* this counter limits the number of iterations to find a better
       candidate */
    unsigned i = 0;
    constexpr unsigned MAX_ITERATIONS = 64;

    for (auto &hole : holes) {
        if (hole.size >= size && (best == nullptr || hole.size < best->size)) {
            /* this is a better candidate: big enough, but smaller
               than the previous candidate */
            best = &hole;

            if (hole.size == size)
                /* can't get any better, stop now */
                break;
        }

        if (best != nullptr && ++i >= MAX_ITERATIONS)
            break;
    }

    return best;
}

Rubber::Hole *
Rubber::FindHole(size_t size) noexcept
{
    unsigned bucket = rubber_hole_threshold_lookup(size);

    auto *h = FindHole(holes[bucket], size);
    if (h == nullptr) {
        while (bucket > 0) {
            --bucket;
            if (!holes[bucket].empty()) {
                h = &holes[bucket].front();
                assert(h->size > size);
                break;
            }
        }
    }

    return h;
}

void
Rubber::AddToHoleList(Hole &hole) noexcept
{
    holes[rubber_hole_threshold_lookup(hole.size)].push_front(hole);
}

void
Rubber::AddHole(size_t offset, size_t size,
                unsigned previous_id, unsigned next_id) noexcept
{
    auto *hole = (Hole *)WriteAt(offset);
    hole->size = size;
    hole->previous_id = previous_id;
    hole->next_id = next_id;

    AddToHoleList(*hole);
}

void
Rubber::AddHoleAfter(unsigned reference_id, size_t offset, size_t size) noexcept
{
    const auto &o = table->entries[reference_id];
    assert(o.allocated);
    assert(o.next != 0);

    const unsigned next_id = o.next;
    const auto &next = table->entries[next_id];
    assert(next.allocated);
    assert(next.offset > offset);
    assert(next.offset >= offset + size);

    const size_t reference_end = o.GetEndOffset();

    assert(offset >= reference_end);

    if (offset > reference_end) {
        /* follows an existing hole: grow the existing one */
        auto &hole = *(Hole *)WriteAt(reference_end);
        assert(reference_end + hole.size == offset);
        assert(hole.previous_id == reference_id);

        RemoveHole(hole);

        hole.size += size;
        hole.next_id = next_id;

        if (reference_end + hole.size < next.offset) {
            /* there's another hole to merge with */
            auto &next_hole = *(Hole *)
                WriteAt(reference_end + hole.size);
            assert(reference_end + hole.size + next_hole.size == next.offset);
            assert(next_hole.next_id == next_id);

            RemoveHole(next_hole);
            hole.size += next_hole.size;
        }

        AddToHoleList(hole);
    } else if (offset + size < next.offset) {
        /* precedes an existing hole: merge the new hole and the
           existing one */
        auto &next_hole = *(Hole *)WriteAt(offset + size);
        assert(offset + size + next_hole.size == next.offset);
        assert(next_hole.next_id == next_id);

        RemoveHole(next_hole);

        AddHole(offset, size + next_hole.size, reference_id, next_id);
    } else {
        /* no existing hole before or after the new one */
        AddHole(offset, size, reference_id, next_id);
    }
}

/*
 * rubber
 *
 */

Rubber::Rubber(size_t _max_size) noexcept
    :max_size(HUGE_PAGE_SIZE + AlignHugePageUp(_max_size)), netto_size(0),
     allocation(max_size),
     table((RubberTable *)allocation.get()) {
    static_assert(RUBBER_ALIGN >= sizeof(Hole), "Alignment too large");

    table->Init(max_size / 1024);
    const size_t table_size = table->GetSize();
    mmap_enable_huge_pages(WriteAt(table_size),
                           AlignHugePageDown(max_size - table_size));
}

Rubber *
rubber_new(size_t size)
{
    return new Rubber(size);
}

void
rubber_free(Rubber *r) noexcept
{
    delete r;
}

void
rubber_fork_cow(Rubber *r, bool inherit) noexcept
{
    r->ForkCow(inherit);
}

void
Rubber::UseHole(Hole &hole, unsigned id, size_t size) noexcept
{
    const unsigned previous_id = hole.previous_id;
    const unsigned next_id = hole.next_id;

    table->Link(id, previous_id, next_id);

    RemoveHole(hole);

    if (size != hole.size) {
        /* shrink the hole */

        void *p = (uint8_t *)&hole + size;
        auto &new_hole = *(Hole *)p;

        new_hole.size = hole.size - size;
        new_hole.previous_id = id;
        new_hole.next_id = next_id;

        AddToHoleList(new_hole);
    }
}

inline unsigned
Rubber::AddInHole(Hole &hole, size_t size) noexcept
{
    unsigned id = table->AddId();
    if (id == 0)
        return 0;

    auto &o = table->entries[id];
    o.Init(OffsetOf(hole), size);

    UseHole(hole, id, size);

    netto_size += size;

    return id;
}

unsigned
Rubber::AddInHole(size_t size) noexcept
{
    auto *hole = FindHole(size);
    return hole != nullptr
        /* found a hole */
        ? AddInHole(*hole, size)
        /* no hole found */
        : 0;
}

inline bool
Rubber::MoveLast(size_t max_object_size) noexcept
{
    const auto id = table->entries[0].previous;
    const auto t = table;
    auto &o = t->entries[id];
    if (o.size > max_object_size)
        /* too large */
        return false;

    assert(o.next == 0);
    const auto hole = FindHole(o.size);
    if (hole == nullptr || hole->next_id == id)
        /* no hole found */
        return false;

    const auto previous_id = o.previous;
    auto &previous = table->entries[previous_id];
    assert(previous.next == id);
    assert(previous.GetEndOffset() <= o.offset);

    /* any hole that may exist before this object is obsolete ... */
    auto *hole2 = FindHoleBetween(previous, o);
    if (hole2 != nullptr) {
        /* ... so remove it */
        assert(hole2->previous_id == previous_id);
        assert(hole2->next_id == id);
        assert(previous.GetEndOffset() + hole2->size == o.offset);

        RemoveHole(*hole2);
    }

    /* remove this object from the ordered linked list */
    table->Unlink(id);

    /* replace the hole we found earlier */
    const auto old_offset = o.offset;
    const auto new_offset = o.offset = OffsetOf(*hole);
    const auto size = o.size;

    UseHole(*hole, id, size);

    /* move data to that hole */
    memcpy(WriteAt(new_offset), ReadAt(old_offset), size);

    return true;
}

unsigned
Rubber::Add(size_t size) noexcept
{
    assert(netto_size + GetTotalHoleSize() == GetBruttoSize());
    assert(size > 0);

    if (size >= max_size)
        /* sanity check to avoid integer overflows */
        return 0;

    size = align_size(size);

    if (netto_size + size <= GetBruttoSize()) {
        unsigned id = AddInHole(size);
        if (id != 0)
            return id;
    }

    if (GetBruttoSize() / 3 >= netto_size)
        /* auto-compress when a lot of allocations have been freed */
        Compress();
    else
        while (MoveLast(size - 1)) {}

    size_t offset = table->GetTailOffset();
    if (offset + size > max_size) {
        /* compress, then try again */
        Compress();

        offset = table->GetTailOffset();
        if (offset + size > max_size)
            /* no, sorry, there's simply not enough free memory */
            return 0;
    }

    const unsigned id = table->Add(offset, size);
    if (id > 0) {
        netto_size += size;
    }

    assert(netto_size + GetTotalHoleSize() == GetBruttoSize());

    return id;
}

unsigned
rubber_add(Rubber *r, size_t size) noexcept
{
    assert(r != nullptr);

    return r->Add(size);
}

size_t
rubber_size_of(const Rubber *r, unsigned id) noexcept
{
    assert(r != nullptr);

    return r->GetSizeOf(id);
}

void *
rubber_write(Rubber *r, unsigned id) noexcept
{
    assert(r != nullptr);

    return r->Write(id);
}

const void *
rubber_read(const Rubber *r, unsigned id) noexcept
{
    assert(r != nullptr);

    return r->Read(id);
}

inline void
Rubber::Shrink(unsigned id, size_t new_size) noexcept
{
    assert(netto_size + GetTotalHoleSize() == GetBruttoSize());
    assert(new_size > 0);

    RubberObject *const o = &table->entries[id];
    assert(o->allocated);
    assert(new_size <= o->size);

    new_size = align_size(new_size);

    if (new_size == o->size)
        return;

    const size_t hole_offset = o->offset + new_size;
    const size_t hole_size = o->size - new_size;

    size_t delta = table->Shrink(id, new_size);
    netto_size -= delta;

    if (o->next != 0)
        AddHoleAfter(id, hole_offset, hole_size);

    assert(netto_size + GetTotalHoleSize() == GetBruttoSize());
}

void
rubber_shrink(Rubber *r, unsigned id, size_t new_size) noexcept
{
    assert(r != nullptr);

    return r->Shrink(id, new_size);
}

inline void
Rubber::DiscardHoleBetween(RubberObject &a, RubberObject &b) noexcept
{
    assert(a.GetEndOffset() <= b.offset);

    auto *hole = FindHoleBetween(a, b);
    if (hole != nullptr) {
        assert(hole->previous_id == table->IdOf(a));
        assert(a.GetEndOffset() + hole->size == b.offset);

        RemoveHole(*hole);
    }
}

inline void
Rubber::ReplaceWithHole(RubberObject &o,
                        unsigned previous_id, unsigned next_id) noexcept
{
    if (next_id == 0) {
        /* this is the last allocation */
        /* remove the hole before this */
        DiscardHoleBetween(table->entries[previous_id], o);
    } else
        AddHoleAfter(previous_id, o.offset, o.size);
}

inline void
Rubber::Remove(unsigned id) noexcept
{
    assert(id > 0);

    auto &o = table->entries[id];
    assert(o.allocated);

    const unsigned previous_id = o.previous;
    const unsigned next_id = o.next;

    size_t size = table->Remove(id);
    assert(netto_size >= size);

    netto_size -= size;

    ReplaceWithHole(o, previous_id, next_id);
}

void
rubber_remove(Rubber *r, unsigned id) noexcept
{
    return r->Remove(id);
}

size_t
rubber_get_max_size(const Rubber *r) noexcept
{
    return r->GetMaxSize();
}

size_t
rubber_get_brutto_size(const Rubber *r) noexcept
{
    return r->GetBruttoSize();
}

size_t
rubber_get_netto_size(const Rubber *r) noexcept
{
    return r->GetNettoSize();
}

inline void
Rubber::MoveData(RubberObject &o, size_t new_offset) noexcept
{
    assert(new_offset <= o.offset);
    assert(o.size > 0);

    if (o.offset == new_offset)
        return;

    memmove(WriteAt(new_offset), ReadAt(o.offset), o.size);
    o.offset = new_offset;
}

AllocatorStats
rubber_get_stats(const Rubber &r) noexcept
{
    AllocatorStats stats;
    stats.brutto_size = r.GetBruttoSize();
    stats.netto_size = r.GetNettoSize();
    return stats;
}

void
Rubber::Compress() noexcept
{
    assert(GetBruttoSize() >= netto_size);
    assert(netto_size + GetTotalHoleSize() == GetBruttoSize());

    if (GetBruttoSize() == netto_size) {
#ifndef NDEBUG
        for (const auto &i : holes)
            assert(i.empty());
#endif
        return;
    }

    for (auto &i : holes)
        i.clear();

    /* relocate all items, eliminate spaces */

    RubberObject *o = table->GetHead();
    assert(o->offset == 0);
    size_t offset = o->size;

    while ((o = table->GetNext(o)) != nullptr) {
        MoveData(*o, offset);
        offset += o->size;
    }

    assert(offset == netto_size + table->GetSize());
    assert(netto_size == GetBruttoSize());

    /* tell the kernel that we won't need the data after our last
       allocation */
    const size_t allocated = AlignHugePageUp(offset);
    if (allocated < max_size)
        mmap_discard_pages(WriteAt(allocated), max_size - allocated);
}

void
rubber_compress(Rubber *r) noexcept
{
    assert(r != nullptr);

    r->Compress();
}
