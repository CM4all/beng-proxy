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
#include "system/HugePage.hxx"
#include "system/mmap.h"
#include "AllocatorStats.hxx"

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
    unsigned initialized_tail = 1;

    /**
     * The index of the first free table entry.  The linked list
     * contains all free entries in no specific order.  This is 0 if
     * the table is full.
     */
    unsigned free_head = 0;

    /**
     * The first entry (index 0) is the table itself.  Its "previous"
     * attribute is the index of the allocated object with the largest
     * offset.
     */
    RubberObject entries[1];

    explicit RubberTable(unsigned _max_entries) noexcept;
    ~RubberTable() noexcept;

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

    gcc_pure
    RubberObject &GetTail() noexcept {
        return entries[entries[0].previous];
    }

    gcc_pure
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

static const size_t RUBBER_ALIGN = 0x20;

static constexpr inline void *
align_page_size_ptr(void *p) noexcept
{
    return (void *)(long)AlignHugePageUp((size_t)p);
}

static constexpr inline size_t
align_size(size_t size) noexcept
{
    return ((size - 1) | (RUBBER_ALIGN - 1)) + 1;
}

/*
 * rubber_table
 *
 */

inline
RubberTable::RubberTable(unsigned _max_entries) noexcept
{
    assert(_max_entries > 1);

    uint8_t *const table_begin = (uint8_t *)this;

    /* round to nearest "huge page", so the first real allocation
       starts at a "huge page" boundary */
    uint8_t *const table_end = (uint8_t *)
        align_page_size_ptr(table_begin + RequiredSize(_max_entries));
    const size_t table_size = table_end - table_begin;

    entries[0].InitHead(table_size);

    max_entries = Capacity(table_size);

#ifndef NDEBUG
    entries[0].allocated = true;
#endif
}

constexpr size_t Rubber::HOLE_THRESHOLDS[];

inline
RubberTable::~RubberTable() noexcept
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
    unsigned bucket = LookupHoleThreshold(size);

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
    holes[LookupHoleThreshold(hole.size)].push_front(hole);
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

Rubber::Rubber(size_t _max_size)
    :table(HUGE_PAGE_SIZE + AlignHugePageUp(_max_size),
           (HUGE_PAGE_SIZE + AlignHugePageUp(_max_size)) / 1024u)
{
    static_assert(RUBBER_ALIGN >= sizeof(Hole), "Alignment too large");

    const size_t table_size = table->GetSize();
    mmap_enable_huge_pages(WriteAt(table_size),
                           AlignHugePageDown(table.size() - table_size));
}

Rubber::~Rubber() noexcept
{
    assert(table->IsEmpty());
    assert(netto_size == 0);
}

void
Rubber::ForkCow(bool inherit) noexcept
{
    mmap_enable_fork(table.get(), table.size(), inherit);
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
    const auto t = table.get();
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

    if (size >= table.size())
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
    if (offset + size > table.size()) {
        /* compress, then try again */
        Compress();

        offset = table->GetTailOffset();
        if (offset + size > table.size())
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

size_t
Rubber::GetSizeOf(unsigned id) const noexcept
{
    assert(id > 0);

    return table->GetSizeOf(id);
}

void *
Rubber::Write(unsigned id) noexcept
{
    const size_t offset = table->GetOffsetOf(id);
    assert(offset < table.size());
    return WriteAt(offset);
}

const void *
Rubber::Read(unsigned id) const noexcept
{
    const size_t offset = table->GetOffsetOf(id);
    assert(offset < table.size());
    return ReadAt(offset);
}

void
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

inline Rubber::Hole *
Rubber::FindHoleBetween(RubberObject &a, RubberObject &b) noexcept
{
    assert(a.offset < b.offset);

    return a.GetEndOffset() < b.offset
        ? (Hole *)WriteAt(a.GetEndOffset())
        : nullptr;
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

void
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

size_t
Rubber::GetMaxSize() const noexcept
{
    return table.size() - table->GetSize();
}

size_t
Rubber::GetBruttoSize() const noexcept
{
    return table->GetBruttoSize();
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
Rubber::GetStats() const noexcept
{
    AllocatorStats stats;
    stats.brutto_size = GetBruttoSize();
    stats.netto_size = GetNettoSize();
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
    if (allocated < table.size())
        mmap_discard_pages(WriteAt(allocated), table.size() - allocated);
}
