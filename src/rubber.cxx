/*
 * The "rubber" memory allocator.  It is a buffer for storing many
 * large objects.  Unlike heap memory, unused areas are given back to
 * the operating system.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rubber.hxx"
#include "mmap.h"

#include <inline/list.h>

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
     * The index of the allocated object with the largest offset (the
     * "head" is always 0, which points to the table).  The linked
     * list is sorted by offset.
     */
    unsigned allocated_tail;

    /**
     * The index of the first free table entry.  The linked list
     * contains all free entries in no specific order.  This is 0 if
     * the table is full.
     */
    unsigned free_head;

    /**
     * The first entry (index 0) is the table itself.
     */
    RubberObject entries[1];

    size_t Init(unsigned _max_entries);
    void Deinit();

    bool IsEmpty() const {
        return allocated_tail == 0;
    }

    /**
     * Calculate the size [in bytes] of a #rubber_table struct for the
     * given number of entries.
     */
    gcc_const
    static size_t RequiredSize(unsigned n) {
        assert(n > 0);

        const RubberTable *dummy = nullptr;
        return sizeof(*dummy) + sizeof(dummy->entries) * (n - 1);
    }

    /**
     * Calculate the capacity [in number of entries] of a #rubber_table
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
        const RubberObject *tail = &entries[allocated_tail];

        return tail->offset + tail->size - GetSize();
    }

    RubberObject *GetHead() {
        return &entries[0];
    }

    RubberObject *GetNext(RubberObject *o) {
        return o->next != 0
            ? &entries[o->next]
            : nullptr;
    }

    gcc_pure
    size_t GetTailOffset() const {
        assert(allocated_tail < max_entries);

        const RubberObject *tail = &entries[allocated_tail];
        assert(tail->next == 0);

        return tail->offset + tail->size;
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
     * The sibling holes in the list (#rubber.holes[i]).
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
    sizeof(RUBBER_HOLE_THRESHOLDS) / sizeof(RUBBER_HOLE_THRESHOLDS[0]);

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
    list_head holes[N_RUBBER_HOLE_THRESHOLDS];

public:
    Rubber(size_t _max_size, RubberTable *_table);

    ~Rubber() {
        assert(table->IsEmpty());
        assert(netto_size == 0);

        table->Deinit();
        mmap_free(table, max_size);
    }
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
    allocated_tail = 0;

    uint8_t *const table_begin = (uint8_t *)this;

    /* round to nearest "huge page", so the first real allocation
       starts at a "huge page" boundary */
    uint8_t *const table_end = (uint8_t *)
        align_page_size_ptr(table_begin + RequiredSize(_max_entries));
    const size_t table_size = table_end - table_begin;

    entries[0] = (RubberObject){
        .next = 0,
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
    assert(allocated_tail == 0);
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
        RubberObject *o = &entries[id];
        assert(!o->allocated);
        free_head = o->next;
        return id;
    }
}

unsigned
RubberTable::Add(size_t offset, size_t size)
{
    assert(allocated_tail < max_entries);

    unsigned id = AddId();
    if (id == 0)
        return 0;

    RubberObject *o = &entries[id];

    *o = (RubberObject){
        .next = 0,
        .previous = allocated_tail,
        .offset = offset,
        .size = size,
#ifndef NDEBUG
        .allocated = true,
#endif
    };

    /* .. and append it to the "allocated" list */

    RubberObject *tail = &entries[allocated_tail];
    assert(tail->allocated);
    assert(tail->next == 0);
    assert(IsEmpty() ||
           entries[tail->previous].next == allocated_tail);
    assert(offset == tail->offset + tail->size);

    allocated_tail = tail->next = id;

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

    RubberObject *o = &entries[id];
    assert(o->allocated);

    if (o->next != 0) {
        assert(id != allocated_tail);

        RubberObject *next = &entries[o->next];
        assert(next->allocated);
        assert(next->offset > o->offset);
        assert(next->previous == id);

        next->previous = o->previous;
    } else {
        assert(id == allocated_tail);

        allocated_tail = o->previous;
    }

    const unsigned previous_id = o->previous;
    RubberObject *previous = &entries[previous_id];
    assert(previous->allocated);
    assert(previous->offset < o->offset);
    assert(previous->next == id);

    previous->next = o->next;

    /* add it to the "free" list */

    o->next = free_head;
    free_head = id;

#ifndef NDEBUG
    o->allocated = false;
#endif

    return o->size;
}

size_t
RubberTable::GetOffsetOf(unsigned id) const
{
    assert(GetSize() >= sizeof(*this));
    assert(id > 0);
    assert(id < max_entries);
    assert(id < initialized_tail);

    const RubberObject *o = &entries[id];
    assert(o->offset > 0);
    assert(o->offset >= GetSize());
    assert(entries[o->previous].offset < o->offset);
    assert(o->next == 0 || entries[o->next].offset > o->offset);
    assert(o->next == 0 || entries[o->next].offset >= o->offset + o->size);

    return o->offset;
}

/*
 * utilities
 *
 */

static void *
rubber_write_at(Rubber *r, size_t offset)
{
    assert(offset <= r->max_size);

    return ((uint8_t *)r->table) + offset;
}

static const void *
rubber_read_at(const Rubber *r, size_t offset)
{
    assert(offset <= r->max_size);

    return ((const uint8_t *)r->table) + offset;
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
static size_t
rubber_total_hole_size(const Rubber *r)
{
    size_t result = 0;

    for (unsigned i = 0; i < N_RUBBER_HOLE_THRESHOLDS; ++i)
        result += rubber_total_hole_list_size(&r->holes[i]);

    return result;
}

#endif

static size_t
rubber_hole_offset(const Rubber *r, const RubberHole *hole)
{
    return (const uint8_t *)hole - (const uint8_t *)r->table;
}

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

    for (RubberHole *h = (RubberHole *)holes->next;
         &h->siblings != holes; h = (RubberHole *)h->siblings.next)
        if (h->size >= size)
            return h;

    return nullptr;
}

static RubberHole *
rubber_find_hole(Rubber *r, size_t size)
{
    list_head *holes = &r->holes[rubber_hole_threshold_lookup(size)];

    RubberHole *h = rubber_find_hole2(holes, size);
    if (h == nullptr) {
        while (holes > r->holes) {
            --holes;
            if (!list_empty(holes)) {
                h = (RubberHole *)holes->next;
                break;
            }
        }
    }

    return h;
}

static void
rubber_hole_list_add(Rubber *r, RubberHole *hole)
{
    list_add(&hole->siblings,
             &r->holes[rubber_hole_threshold_lookup(hole->size)]);
}

static void
rubber_add_hole_after(Rubber *r, unsigned reference_id,
                      size_t offset, size_t size)
{
    const RubberTable *const t = r->table;
    const RubberObject *const o = &t->entries[reference_id];
    assert(o->allocated);
    assert(o->next != 0);

    const unsigned next_id = o->next;
    const RubberObject *const next = &t->entries[next_id];
    assert(next->allocated);
    assert(next->offset > offset);
    assert(next->offset >= offset + size);

    size_t reference_end = o->offset + o->size;

    assert(offset >= reference_end);

    if (offset > reference_end) {
        /* follows an existing hole: grow the existing one */
        RubberHole *hole = (RubberHole *)rubber_write_at(r, reference_end);
        assert(hole->siblings.prev->next == &hole->siblings);
        assert(hole->siblings.next->prev == &hole->siblings);
        assert(reference_end + hole->size == offset);
        assert(hole->previous_id == reference_id);

        list_remove(&hole->siblings);

        hole->size += size;
        hole->next_id = next_id;

        if (reference_end + hole->size < next->offset) {
            /* there's another hole to merge with */
            RubberHole *next_hole = (RubberHole *)
                rubber_write_at(r, reference_end + hole->size);
            assert(next_hole->siblings.next->prev == &next_hole->siblings);
            assert(reference_end + hole->size + next_hole->size == next->offset);
            assert(next_hole->next_id == next_id);

            list_remove(&next_hole->siblings);
            hole->size += next_hole->size;
        }

        rubber_hole_list_add(r, hole);
    } else if (offset + size < next->offset) {
        /* precedes an existing hole: merge the new hole and the
           existing one */
        RubberHole *next_hole = (RubberHole *)
            rubber_write_at(r, offset + size);
        assert(next_hole->siblings.prev->next == &next_hole->siblings);
        assert(next_hole->siblings.next->prev == &next_hole->siblings);
        assert(offset + size + next_hole->size == next->offset);
        assert(next_hole->next_id == next_id);

        list_remove(&next_hole->siblings);

        RubberHole *hole = (RubberHole *)rubber_write_at(r, offset);
        hole->size = size + next_hole->size;
        hole->previous_id = reference_id;
        hole->next_id = next_id;

        rubber_hole_list_add(r, hole);
    } else {
        /* no existing hole before or after the new one */
        RubberHole *hole = (RubberHole *)rubber_write_at(r, offset);
        hole->size = size;
        hole->previous_id = reference_id;
        hole->next_id = next_id;

        rubber_hole_list_add(r, hole);
    }
}

/*
 * rubber
 *
 */

Rubber::Rubber(size_t _max_size, RubberTable *_table)
    :max_size(_max_size), netto_size(0), table(_table) {
    for (unsigned i = 0; i < N_RUBBER_HOLE_THRESHOLDS; ++i)
        list_init(&holes[i]);

    const size_t table_size = table->Init(max_size / 1024);
    mmap_enable_huge_pages(rubber_write_at(this, table_size),
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

/**
 * Try to find a hole between two objects, and insert a new
 * object there.
 *
 * @return the object id, or 0 on error
 */
static unsigned
rubber_add_in_hole(Rubber *r, size_t size)
{
    assert(r != nullptr);

    RubberHole *hole = rubber_find_hole(r, size);
    if (hole == nullptr)
        /* no hole found */
        return 0;

    /* found a hole */

    const unsigned previous_id = hole->previous_id;
    const unsigned next_id = hole->next_id;

    unsigned id = r->table->AddId();
    if (id == 0)
        return 0;

    RubberObject *const o = &r->table->entries[id];
    *o = (RubberObject){
        .next = next_id,
        .previous = previous_id,
        .offset = rubber_hole_offset(r, hole),
        .size = size,
#ifndef NDEBUG
        .allocated = true,
#endif
    };

    RubberObject *const previous = &r->table->entries[previous_id];
    RubberObject *const next = &r->table->entries[next_id];

    assert(previous->next == next_id);
    assert(next->previous == previous_id);

    previous->next = id;
    next->previous = id;

    list_remove(&hole->siblings);

    if (size != hole->size) {
        /* shrink the hole */

        void *p = (uint8_t *)hole + size;
        RubberHole *new_hole = (RubberHole *)p;

        new_hole->size = hole->size - size;
        new_hole->previous_id = id;
        new_hole->next_id = next_id;

        rubber_hole_list_add(r, new_hole);
    }

    r->netto_size += size;

    return id;
}

unsigned
rubber_add(Rubber *r, size_t size)
{
    assert(r != nullptr);
    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));
    assert(size > 0);

    if (size >= r->max_size)
        /* sanity check to avoid integer overflows */
        return 0;

    size = align_size(size);

    if (r->netto_size + size <= rubber_get_brutto_size(r)) {
        unsigned id = rubber_add_in_hole(r, size);
        if (id != 0)
            return id;
    }

    if (rubber_get_brutto_size(r) / 3 >= r->netto_size)
        /* auto-compress when a lot of allocations have been freed */
        rubber_compress(r);

    size_t offset = r->table->GetTailOffset();
    if (offset + size > r->max_size) {
        /* compress, then try again */
        rubber_compress(r);

        offset = r->table->GetTailOffset();
        if (offset + size > r->max_size)
            /* no, sorry, there's simply not enough free memory */
            return 0;
    }

    const unsigned id = r->table->Add(offset, size);
    if (id > 0) {
        r->netto_size += size;
    }

    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));

    return id;
}

size_t
rubber_size_of(const Rubber *r, unsigned id)
{
    assert(r != nullptr);
    assert(id > 0);

    return r->table->GetSizeOf(id);
}

void *
rubber_write(Rubber *r, unsigned id)
{
    const size_t offset = r->table->GetOffsetOf(id);
    assert(offset < r->max_size);
    return rubber_write_at(r, offset);
}

const void *
rubber_read(const Rubber *r, unsigned id)
{
    const size_t offset = r->table->GetOffsetOf(id);
    assert(offset < r->max_size);
    return rubber_read_at(r, offset);
}

void
rubber_shrink(Rubber *r, unsigned id, size_t new_size)
{
    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));
    assert(new_size > 0);

    RubberObject *const o = &r->table->entries[id];
    assert(o->allocated);
    assert(new_size <= o->size);

    new_size = align_size(new_size);

    if (new_size == o->size)
        return;

    const size_t hole_offset = o->offset + new_size;
    const size_t hole_size = o->size - new_size;

    size_t delta = r->table->Shrink(id, new_size);
    r->netto_size -= delta;

    if (o->next != 0)
        rubber_add_hole_after(r, id, hole_offset, hole_size);

    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));
}

void
rubber_remove(Rubber *r, unsigned id)
{
    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));
    assert(id > 0);

    RubberObject *const o = &r->table->entries[id];
    assert(o->allocated);

    const unsigned previous_id = o->previous;
    const unsigned next_id = o->next;

    size_t size = r->table->Remove(id);
    assert(r->netto_size >= size);

    r->netto_size -= size;

    if (next_id == 0) {
        /* this is the last allocation */

        /* remove the hole before this */
        RubberObject *previous = &r->table->entries[previous_id];
        assert(previous->offset + previous->size <= o->offset);

        if (previous->offset + previous->size < o->offset) {
            RubberHole *hole = (RubberHole *)
                rubber_write_at(r, previous->offset + previous->size);
            assert(hole->previous_id == previous_id);
            assert(previous->offset + previous->size + hole->size == o->offset);

            list_remove(&hole->siblings);
        }
    } else
        rubber_add_hole_after(r, previous_id, o->offset, o->size);

    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));
}

size_t
rubber_get_max_size(const Rubber *r)
{
    return r->max_size - r->table->GetSize();
}

size_t
rubber_get_brutto_size(const Rubber *r)
{
    return r->table->GetBruttoSize();
}

size_t
rubber_get_netto_size(const Rubber *r)
{
    return r->netto_size;
}

static void
rubber_relocate(Rubber *r, RubberObject *o, size_t offset)
{
    assert(offset <= o->offset);
    assert(o->size > 0);

    if (o->offset == offset)
        return;

    void *dest = rubber_write_at(r, offset);
    const void *src = rubber_read_at(r, o->offset);

    memmove(dest, src, o->size);
    o->offset = offset;
}

void
rubber_compress(Rubber *r)
{
    assert(r != nullptr);

    assert(rubber_get_brutto_size(r) >= r->netto_size);
    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));

    if (rubber_get_brutto_size(r) == r->netto_size) {
#ifndef NDEBUG
        for (unsigned i = 0; i < N_RUBBER_HOLE_THRESHOLDS; ++i)
            assert(list_empty(&r->holes[i]));
#endif
        return;
    }

    for (unsigned i = 0; i < N_RUBBER_HOLE_THRESHOLDS; ++i)
        list_init(&r->holes[i]);

    /* relocate all items, eliminate spaces */

    RubberObject *o = r->table->GetHead();
    assert(o->offset == 0);
    size_t offset = o->size;

    while ((o = r->table->GetNext(o)) != nullptr) {
        rubber_relocate(r, o, offset);
        offset += o->size;
    }

    assert(offset == r->netto_size + r->table->GetSize());
    assert(r->netto_size == rubber_get_brutto_size(r));

    /* tell the kernel that we won't need the data after our last
       allocation */
    const size_t allocated = align_page_size(offset);
    if (allocated < r->max_size)
        mmap_discard_pages(rubber_write_at(r, allocated),
                           r->max_size - allocated);
}
