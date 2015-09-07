/*
 * The "rubber" memory allocator.  It is a buffer for storing many
 * large objects.  Unlike heap memory, unused areas are given back to
 * the operating system.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rubber.hxx"
#include "mmap.h"
#include "AllocatorStats.hxx"
#include "util/Macros.hxx"

#include <inline/list.h>

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

    constexpr size_t GetEndOffset() const {
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

    size_t Init(unsigned _max_entries);
    void Deinit();

    bool IsEmpty() const {
        return entries[0].next == 0;
    }

    unsigned IdOf(const RubberObject &o) const {
        assert(&o >= entries);
        assert(&o < &entries[max_entries]);

        return &o - entries;
    }

    /**
     * Calculate the size [in bytes] of a #RubberTable struct for the
     * given number of entries.
     */
    gcc_const
    static size_t RequiredSize(unsigned n) {
        assert(n > 0);

        const RubberTable *dummy = nullptr;
        return sizeof(*dummy) + sizeof(dummy->entries) * (n - 1);
    }

    /**
     * Calculate the capacity [in number of entries] of a #RubberTable
     * struct for the given size [in bytes].
     */
    gcc_const
    static unsigned Capacity(size_t size) {
        const RubberTable *dummy = nullptr;
        assert(size >= sizeof(*dummy));

        return (size - sizeof(*dummy)) / sizeof(dummy->entries) + 1;
    }

    /**
     * Returns the allocated size of the table object.  At the same time,
     * this is the offset of the first allocation.
     */
    gcc_pure
    size_t GetSize() const {
        assert(entries[0].offset == 0);

        return entries[0].size;
    }

    gcc_pure
    size_t GetBruttoSize() const {
        return GetTail().GetEndOffset() - GetSize();
    }

    RubberObject *GetHead() {
        return &entries[0];
    }

    RubberObject *GetNext(RubberObject *o) {
        return o->next != 0
            ? &entries[o->next]
            : nullptr;
    }

    gcc_const
    RubberObject &GetTail() {
        return entries[entries[0].previous];
    }

    gcc_const
    const RubberObject &GetTail() const {
        return entries[entries[0].previous];
    }

    gcc_pure
    size_t GetTailOffset() const {
        const auto &tail = GetTail();
        assert(tail.next == 0);

        return tail.GetEndOffset();
    }

    /**
     * Allocate a new object id.  The caller must initialise the object.
     */
    unsigned AddId();
    unsigned Add(size_t offset, size_t size);
    size_t Remove(unsigned id);

    gcc_pure
    size_t GetSizeOf(unsigned id) const;

    gcc_pure
    size_t GetOffsetOf(unsigned id) const;

    size_t Shrink(unsigned id, size_t new_size);
};

struct RubberHole {
    /**
     * The sibling holes in the list (#Rubber.holes[i]).
     */
    list_head siblings;

    /**
     * The size of this hole (including the size of this struct).
     */
    size_t size;

    /**
     * The allocated objects before and after this hole.
     */
    unsigned previous_id, next_id;
};

/**
 * The threshold for each hole list.  The goal is to reduce the cost
 * of searching a hole that fits.
 */
static constexpr size_t RUBBER_HOLE_THRESHOLDS[] = {
    1024 * 1024, 64 * 1024, 32 * 1024, 16 * 1024, 8192, 4096, 2048, 1024, 64, 0
};

static constexpr size_t N_RUBBER_HOLE_THRESHOLDS =
    ARRAY_SIZE(RUBBER_HOLE_THRESHOLDS);

class Rubber {
public:
    /**
     * The maximum size of the memory map.  This is the value passed
     * to rubber_new() and will never be changed.
     */
    const size_t max_size;

    /**
     * The sum of all allocation sizes.
     */
    size_t netto_size;

    /**
     * The table managing the allocations in the memory map.  At the
     * same time, this is the pointer to the memory map.
     */
    RubberTable *const table;

    /**
     * A list of all holes in the buffer.  Each array element hosts
     * its own list with holes at the size of
     * RUBBER_HOLE_THRESHOLDS[i] or bigger.
     */
    std::array<list_head, N_RUBBER_HOLE_THRESHOLDS> holes;

public:
    Rubber(size_t _max_size, RubberTable *_table);

    ~Rubber() {
        assert(table->IsEmpty());
        assert(netto_size == 0);

        table->Deinit();
        mmap_free(table, max_size);
    }

    size_t GetMaxSize() const {
        return max_size - table->GetSize();
    }

    size_t GetNettoSize() const {
        return netto_size;
    }

    size_t GetBruttoSize() const {
        return table->GetBruttoSize();
    }

    gcc_pure
    size_t GetSizeOf(unsigned id) const {
        assert(id > 0);

        return table->GetSizeOf(id);
    }

    gcc_pure
    void *WriteAt(size_t offset) {
        assert(offset <= max_size);

        return (uint8_t *)table + offset;
    }

    gcc_pure
    const void *ReadAt(size_t offset) const {
        assert(offset <= max_size);

        return (const uint8_t *)table + offset;
    }

    gcc_pure
    void *Write(unsigned id) {
        const size_t offset = table->GetOffsetOf(id);
        assert(offset < max_size);
        return WriteAt(offset);
    }

    gcc_pure
    const void *Read(unsigned id) const {
        const size_t offset = table->GetOffsetOf(id);
        assert(offset < max_size);
        return ReadAt(offset);
    }

    size_t OffsetOf(const void *p) const {
        return (const uint8_t *)p - (const uint8_t *)table;
    }

    size_t OffsetOf(const RubberHole &hole) const {
        return OffsetOf(&hole);
    }

#ifndef NDEBUG
    size_t GetTotalHoleSize() const;
#endif

    gcc_pure
    RubberHole *FindHole(size_t size);

    void AddToHoleList(RubberHole &hole);

    void AddHole(size_t offset, size_t size,
                 unsigned previous_id, unsigned next_id);
    void AddHoleAfter(unsigned reference_id, size_t offset, size_t size);

    /**
     * Replace the hole with the specified object.  If there is unused
     * space after the object, create a new #RubberHole instance
     * there.
     */
    void UseHole(RubberHole &hole, RubberObject &o, unsigned id, size_t size);

    /**
     * Try to find a hole between two objects, and insert a new object
     * there.
     *
     * @return the object id, or 0 on error
     */
    unsigned AddInHole(size_t size);

    /**
     * Attempt to move the last allocation into a hole.  This is some kind
     * of simplified defragmentation.  It attempts to keep the "brutto"
     * size of this allocator small by filling holes.
     *
     * @param max_size move it only if it's not larger than this size
     */
    bool MoveLast(size_t max_object_size);

    RubberHole *FindHoleBetween(RubberObject &a, RubberObject &b) {
        assert(a.offset < b.offset);

        return a.GetEndOffset() < b.offset
            ? (RubberHole *)WriteAt(a.GetEndOffset())
            : nullptr;
    }

    unsigned Add(size_t size);
    void Remove(unsigned id);

    void Shrink(unsigned id, size_t new_size);

    void MoveData(RubberObject &o, size_t new_offset);
    void Compress();
};

static const size_t RUBBER_ALIGN = 0x20;

gcc_const
static inline size_t
align_page_size(size_t size)
{
    return ((size - 1) | (mmap_huge_page_size() - 1)) + 1;
}

gcc_const
static inline size_t
align_page_size_down(size_t size)
{
    return size & ~(mmap_huge_page_size() - 1);
}

gcc_const
static inline void *
align_page_size_ptr(void *p)
{
    return (void *)(long)align_page_size((size_t)p);
}

gcc_const
static inline size_t
align_size(size_t size)
{
    return ((size - 1) | (RUBBER_ALIGN - 1)) + 1;
}

/*
 * rubber_table
 *
 */

size_t
RubberTable::Init(unsigned _max_entries)
{
    assert(_max_entries > 1);

    initialized_tail = 1;

    uint8_t *const table_begin = (uint8_t *)this;

    /* round to nearest "huge page", so the first real allocation
       starts at a "huge page" boundary */
    uint8_t *const table_end = (uint8_t *)
        align_page_size_ptr(table_begin + RequiredSize(_max_entries));
    const size_t table_size = table_end - table_begin;

    entries[0] = (RubberObject){
        .next = 0,
        .previous = 0,
        .offset = 0,
        .size = table_size,
    };

    max_entries = Capacity(table_size);

    free_head = 0;

#ifndef NDEBUG
    entries[0].allocated = true;
#endif

    return table_size;
}

void
RubberTable::Deinit()
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
RubberTable::AddId()
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

unsigned
RubberTable::Add(size_t offset, size_t size)
{
    unsigned id = AddId();
    if (id == 0)
        return 0;

    unsigned &allocated_tail = entries[0].previous;

    auto &o = entries[id];
    o = (RubberObject){
        .next = 0,
        .previous = allocated_tail,
        .offset = offset,
        .size = size,
#ifndef NDEBUG
        .allocated = true,
#endif
    };

    /* .. and append it to the "allocated" list */

    RubberObject &tail = entries[allocated_tail];
    assert(tail.allocated);
    assert(tail.next == 0);
    assert(IsEmpty() ||
           entries[tail.previous].next == allocated_tail);
    assert(offset == tail.GetEndOffset());

    allocated_tail = tail.next = id;

    /* done */

    return id;
}

size_t
RubberTable::GetSizeOf(unsigned id) const
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
RubberTable::Shrink(unsigned id, size_t new_size)
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

/**
 * @return the size of the allocation
 */
size_t
RubberTable::Remove(unsigned id)
{
    assert(GetSize() >= sizeof(*this));
    assert(id > 0);
    assert(id < max_entries);

    /* remove it from the "allocated" list */

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

    /* add it to the "free" list */

    o.next = free_head;
    free_head = id;

#ifndef NDEBUG
    o.allocated = false;
#endif

    return o.size;
}

size_t
RubberTable::GetOffsetOf(unsigned id) const
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

gcc_pure
static size_t
rubber_total_hole_list_size(const list_head *holes)
{
    size_t result = 0;
    for (const RubberHole *hole = (const RubberHole *)holes->next;
         &hole->siblings != holes;
         hole = (const RubberHole *)hole->siblings.next) {
        assert(hole->siblings.prev->next == &hole->siblings);
        assert(hole->siblings.next->prev == &hole->siblings);
        assert(hole->size > 0);

        result += hole->size;
    }

    return result;
}

gcc_pure
size_t
Rubber::GetTotalHoleSize() const
{
    size_t result = 0;

    for (const auto &i : holes)
        result += rubber_total_hole_list_size(&i);

    return result;
}

#endif

gcc_pure
static unsigned
rubber_hole_threshold_lookup(size_t size)
{
    for (unsigned i = 0;; ++i)
        if (size >= RUBBER_HOLE_THRESHOLDS[i])
            return i;
}

static RubberHole *
rubber_find_hole2(list_head *holes, size_t size)
{
    assert(size >= RUBBER_ALIGN);

    /* the current best candidate */
    RubberHole *best = nullptr;

    /* this counter limits the number of iterations to find a better
       candidate */
    unsigned i = 0;
    constexpr unsigned MAX_ITERATIONS = 64;

    for (RubberHole *h = (RubberHole *)holes->next;
         &h->siblings != holes && (best == nullptr || i < MAX_ITERATIONS);
         h = (RubberHole *)h->siblings.next, ++i) {
        if (h->size >= size && (best == nullptr || h->size < best->size)) {
            /* this is a better candidate: big enough, but smaller
               than the previous candidate */
            best = h;

            if (h->size == size)
                /* can't get any better, stop now */
                break;
        }
    }

    return best;
}

RubberHole *
Rubber::FindHole(size_t size)
{
    unsigned bucket = rubber_hole_threshold_lookup(size);

    RubberHole *h = rubber_find_hole2(&holes[bucket], size);
    if (h == nullptr) {
        while (bucket > 0) {
            --bucket;
            if (!list_empty(&holes[bucket])) {
                h = (RubberHole *)holes[bucket].next;
                assert(h->size > size);
                break;
            }
        }
    }

    return h;
}

void
Rubber::AddToHoleList(RubberHole &hole)
{
    list_add(&hole.siblings,
             &holes[rubber_hole_threshold_lookup(hole.size)]);
}

void
Rubber::AddHole(size_t offset, size_t size,
                unsigned previous_id, unsigned next_id)
{
    RubberHole *hole = (RubberHole *)WriteAt(offset);
    hole->size = size;
    hole->previous_id = previous_id;
    hole->next_id = next_id;

    AddToHoleList(*hole);
}

void
Rubber::AddHoleAfter(unsigned reference_id, size_t offset, size_t size)
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
        auto &hole = *(RubberHole *)WriteAt(reference_end);
        assert(hole.siblings.prev->next == &hole.siblings);
        assert(hole.siblings.next->prev == &hole.siblings);
        assert(reference_end + hole.size == offset);
        assert(hole.previous_id == reference_id);

        list_remove(&hole.siblings);

        hole.size += size;
        hole.next_id = next_id;

        if (reference_end + hole.size < next.offset) {
            /* there's another hole to merge with */
            auto &next_hole = *(RubberHole *)
                WriteAt(reference_end + hole.size);
            assert(next_hole.siblings.next->prev == &next_hole.siblings);
            assert(reference_end + hole.size + next_hole.size == next.offset);
            assert(next_hole.next_id == next_id);

            list_remove(&next_hole.siblings);
            hole.size += next_hole.size;
        }

        AddToHoleList(hole);
    } else if (offset + size < next.offset) {
        /* precedes an existing hole: merge the new hole and the
           existing one */
        auto &next_hole = *(RubberHole *)WriteAt(offset + size);
        assert(next_hole.siblings.prev->next == &next_hole.siblings);
        assert(next_hole.siblings.next->prev == &next_hole.siblings);
        assert(offset + size + next_hole.size == next.offset);
        assert(next_hole.next_id == next_id);

        list_remove(&next_hole.siblings);

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

Rubber::Rubber(size_t _max_size, RubberTable *_table)
    :max_size(_max_size), netto_size(0), table(_table) {
    for (auto &i : holes)
        list_init(&i);

    const size_t table_size = table->Init(max_size / 1024);
    mmap_enable_huge_pages(WriteAt(table_size),
                           align_page_size_down(max_size - table_size));
}

Rubber *
rubber_new(size_t size)
{
    assert(RUBBER_ALIGN >= sizeof(RubberHole));

    size = mmap_huge_page_size() + align_page_size(size);

    void *p = mmap_alloc_anonymous(size);
    if (p == (void *)-1)
        return nullptr;

    return new Rubber(size, (RubberTable *)p);
}

void
rubber_free(Rubber *r)
{
    delete r;
}

void
rubber_fork_cow(Rubber *r, bool inherit)
{
    mmap_enable_fork(r->table, r->max_size, inherit);
}

void
Rubber::UseHole(RubberHole &hole, RubberObject &o, unsigned id, size_t size)
{
    const unsigned previous_id = hole.previous_id;
    const unsigned next_id = hole.next_id;

    o.next = next_id;
    o.previous = previous_id;

    RubberObject *const previous = &table->entries[previous_id];
    RubberObject *const next = &table->entries[next_id];

    assert(previous->next == next_id);
    assert(next->previous == previous_id);

    previous->next = id;
    next->previous = id;

    list_remove(&hole.siblings);

    if (size != hole.size) {
        /* shrink the hole */

        void *p = (uint8_t *)&hole + size;
        auto &new_hole = *(RubberHole *)p;

        new_hole.size = hole.size - size;
        new_hole.previous_id = id;
        new_hole.next_id = next_id;

        AddToHoleList(new_hole);
    }
}

unsigned
Rubber::AddInHole(size_t size)
{
    RubberHole *hole = FindHole(size);
    if (hole == nullptr)
        /* no hole found */
        return 0;

    /* found a hole */

    unsigned id = table->AddId();
    if (id == 0)
        return 0;

    auto &o = table->entries[id];
    o = (RubberObject){
        .offset = OffsetOf(hole),
        .size = size,
#ifndef NDEBUG
        .allocated = true,
#endif
    };

    UseHole(*hole, o, id, size);

    netto_size += size;

    return id;
}

inline bool
Rubber::MoveLast(size_t max_object_size)
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

        list_remove(&hole2->siblings);
    }

    /* remove this object from the ordered linked list */
    previous.next = 0;
    table->entries[0].previous = previous_id;

    /* replace the hole we found earlier */
    const auto size = o.size;
    UseHole(*hole, o, id, size);

    /* move data to that hole */
    const auto new_offset = OffsetOf(*hole);
    memcpy(WriteAt(new_offset), ReadAt(o.offset), size);
    o.offset = new_offset;

    return true;
}

unsigned
Rubber::Add(size_t size)
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
rubber_add(Rubber *r, size_t size)
{
    assert(r != nullptr);

    return r->Add(size);
}

size_t
rubber_size_of(const Rubber *r, unsigned id)
{
    assert(r != nullptr);

    return r->GetSizeOf(id);
}

void *
rubber_write(Rubber *r, unsigned id)
{
    assert(r != nullptr);

    return r->Write(id);
}

const void *
rubber_read(const Rubber *r, unsigned id)
{
    assert(r != nullptr);

    return r->Read(id);
}

inline void
Rubber::Shrink(unsigned id, size_t new_size)
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
rubber_shrink(Rubber *r, unsigned id, size_t new_size)
{
    assert(r != nullptr);

    return r->Shrink(id, new_size);
}

inline void
Rubber::Remove(unsigned id)
{
    assert(netto_size + GetTotalHoleSize() == GetBruttoSize());
    assert(id > 0);

    RubberObject *const o = &table->entries[id];
    assert(o->allocated);

    const unsigned previous_id = o->previous;
    const unsigned next_id = o->next;

    size_t size = table->Remove(id);
    assert(netto_size >= size);

    netto_size -= size;

    if (next_id == 0) {
        /* this is the last allocation */

        /* remove the hole before this */
        RubberObject *previous = &table->entries[previous_id];
        assert(previous->GetEndOffset() <= o->offset);

        auto *hole = FindHoleBetween(*previous, *o);
        if (hole != nullptr) {
            assert(hole->previous_id == previous_id);
            assert(previous->GetEndOffset() + hole->size == o->offset);

            list_remove(&hole->siblings);
        }
    } else
        AddHoleAfter(previous_id, o->offset, o->size);

    assert(netto_size + GetTotalHoleSize() == GetBruttoSize());
}

void
rubber_remove(Rubber *r, unsigned id)
{
    return r->Remove(id);
}

size_t
rubber_get_max_size(const Rubber *r)
{
    return r->GetMaxSize();
}

size_t
rubber_get_brutto_size(const Rubber *r)
{
    return r->GetBruttoSize();
}

size_t
rubber_get_netto_size(const Rubber *r)
{
    return r->GetNettoSize();
}

inline void
Rubber::MoveData(RubberObject &o, size_t new_offset)
{
    assert(new_offset <= o.offset);
    assert(o.size > 0);

    if (o.offset == new_offset)
        return;

    memmove(WriteAt(new_offset), ReadAt(o.offset), o.size);
    o.offset = new_offset;
}

AllocatorStats
rubber_get_stats(const Rubber &r)
{
    AllocatorStats stats;
    stats.brutto_size = r.GetBruttoSize();
    stats.netto_size = r.GetNettoSize();
    return stats;
}

void
Rubber::Compress()
{
    assert(GetBruttoSize() >= netto_size);
    assert(netto_size + GetTotalHoleSize() == GetBruttoSize());

    if (GetBruttoSize() == netto_size) {
#ifndef NDEBUG
        for (const auto &i : holes)
            assert(list_empty(&i));
#endif
        return;
    }

    for (auto &i : holes)
        list_init(&i);

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
    const size_t allocated = align_page_size(offset);
    if (allocated < max_size)
        mmap_discard_pages(WriteAt(allocated), max_size - allocated);
}

void
rubber_compress(Rubber *r)
{
    assert(r != nullptr);

    r->Compress();
}
