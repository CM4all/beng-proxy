/*
 * The "rubber" memory allocator.  It is a buffer for storing many
 * large objects.  Unlike heap memory, unused areas are given back to
 * the operating system.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rubber.h"
#include "mmap.h"

#include <inline/list.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct rubber_object {
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

struct rubber_table {
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
    struct rubber_object entries[1];
};

struct rubber_hole {
    /**
     * The sibling holes in the list (#rubber.holes[i]).
     */
    struct list_head siblings;

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
static const size_t RUBBER_HOLE_THRESHOLDS[] = {
    1024 * 1024, 64 * 1024, 32 * 1024, 16 * 1024, 8192, 4096, 2048, 1024, 64, 0
};

#define N_RUBBER_HOLE_THRESHOLDS (sizeof(RUBBER_HOLE_THRESHOLDS) / sizeof(RUBBER_HOLE_THRESHOLDS[0]))

struct rubber {
    /**
     * The maximum size of the memory map.  This is the value passed
     * to rubber_new() and will never be changed.
     */
    size_t max_size;

    /**
     * The sum of all allocation sizes.
     */
    size_t netto_size;

    /**
     * The table managing the allocations in the memory map.  At the
     * same time, this is the pointer to the memory map.
     */
    struct rubber_table *table;

    /**
     * A list of all holes in the buffer.  Each array element hosts
     * its own list with holes at the size of
     * RUBBER_HOLE_THRESHOLDS[i] or bigger.
     */
    struct list_head holes[N_RUBBER_HOLE_THRESHOLDS];
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

/**
 * Calculate the size [in bytes] of a #rubber_table struct for the
 * given number of entries.
 */
gcc_const
static inline size_t
rubber_table_required_size(unsigned n)
{
    assert(n > 0);

    const struct rubber_table *dummy = NULL;
    return sizeof(*dummy) + sizeof(dummy->entries) * (n - 1);
}

/**
 * Calculate the capacity [in number of entries] of a #rubber_table
 * struct for the given size [in bytes].
 */
gcc_const
static inline unsigned
rubber_table_capacity(size_t size)
{
    const struct rubber_table *dummy = NULL;
    assert(size >= sizeof(*dummy));

    return (size - sizeof(*dummy)) / sizeof(dummy->entries) + 1;
}

static size_t
rubber_table_init(struct rubber_table *t, unsigned max_entries)
{
    assert(max_entries > 1);

    t->initialized_tail = 1;
    t->allocated_tail = 0;

    uint8_t *const table_begin = (uint8_t *)t;

    /* round to nearest "huge page", so the first real allocation
       starts at a "huge page" boundary */
    uint8_t *const table_end =
        align_page_size_ptr(table_begin + rubber_table_required_size(max_entries));
    const size_t table_size = table_end - table_begin;

    t->entries[0] = (struct rubber_object){
        .next = 0,
        .offset = 0,
        .size = table_size,
    };

    t->max_entries = rubber_table_capacity(table_size);

    t->free_head = 0;

#ifndef NDEBUG
    t->entries[0].allocated = true;
#endif

    return table_size;
}

static void
rubber_table_deinit(gcc_unused struct rubber_table *t)
{
    assert(t->allocated_tail == 0);
}

gcc_unused
static bool
rubber_table_is_empty(const struct rubber_table *t)
{
    return t->allocated_tail == 0;
}

/**
 * Returns the allocated size of the table object.  At the same time,
 * this is the offset of the first allocation.
 */
gcc_pure gcc_unused
static size_t
rubber_table_size(const struct rubber_table *t)
{
    assert(t->entries[0].offset == 0);

    return t->entries[0].size;
}

gcc_pure
static size_t
rubber_table_get_brutto_size(const struct rubber_table *t)
{
    const struct rubber_object *tail = &t->entries[t->allocated_tail];

    return tail->offset + tail->size - rubber_table_size(t);
}

static struct rubber_object *
rubber_table_head(struct rubber_table *t)
{
    return &t->entries[0];
}

static struct rubber_object *
rubber_table_next(struct rubber_table *t, struct rubber_object *o)
{
    return o->next != 0
        ? &t->entries[o->next]
        : NULL;
}

gcc_pure
static size_t
rubber_table_tail_offset(const struct rubber_table *t)
{
    assert(t != NULL);
    assert(t->allocated_tail < t->max_entries);

    const struct rubber_object *tail = &t->entries[t->allocated_tail];
    assert(tail->next == 0);

    return tail->offset + tail->size;
}

/**
 * Allocate a new object id.  The caller must initialise the object.
 */
static unsigned
rubber_table_add_id(struct rubber_table *t)
{
    if (t->free_head == 0) {
        if (t->initialized_tail >= t->max_entries)
            /* no more entries in the table (though there may still be
               enough space in the memory map) */
            return 0;

        return t->initialized_tail++;
    } else {

        /* remove the first item from the "free" list .. */

        unsigned id = t->free_head;
        struct rubber_object *o = &t->entries[id];
        assert(!o->allocated);
        t->free_head = o->next;
        return id;
    }
}

static unsigned
rubber_table_add(struct rubber_table *t, size_t offset, size_t size)
{
    assert(t != NULL);
    assert(t->allocated_tail < t->max_entries);

    unsigned id = rubber_table_add_id(t);
    if (id == 0)
        return 0;

    struct rubber_object *o = &t->entries[id];

    *o = (struct rubber_object){
        .next = 0,
        .previous = t->allocated_tail,
        .offset = offset,
        .size = size,
#ifndef NDEBUG
        .allocated = true,
#endif
    };

    /* .. and append it to the "allocated" list */

    struct rubber_object *tail = &t->entries[t->allocated_tail];
    assert(tail->allocated);
    assert(tail->next == 0);
    assert(rubber_table_is_empty(t) ||
           t->entries[tail->previous].next == t->allocated_tail);
    assert(offset == tail->offset + tail->size);

    t->allocated_tail = tail->next = id;

    /* done */

    return id;
}

static size_t
rubber_table_size_of(const struct rubber_table *t, unsigned id)
{
    assert(t != NULL);
    assert(t->entries[0].offset == 0);
    assert(t->entries[0].size >= sizeof(*t));
    assert(id > 0);
    assert(id < t->initialized_tail);
    assert(t->entries[id].allocated);

    return t->entries[id].size;
}

/**
 * @return the amount of memory that was freed
 */
static size_t
rubber_table_shrink(struct rubber_table *t, unsigned id, size_t new_size)
{
    assert(t != NULL);
    assert(t->entries[0].offset == 0);
    assert(t->entries[0].size >= sizeof(*t));
    assert(id > 0);
    assert(id < t->initialized_tail);
    assert(t->entries[id].allocated);
    assert(t->entries[id].size >= new_size);

    size_t delta = t->entries[id].size - new_size;
    t->entries[id].size = new_size;

    return delta;
}

/**
 * @return the size of the allocation
 */
static size_t
rubber_table_remove(struct rubber_table *t, unsigned id)
{
    assert(t != NULL);
    assert(rubber_table_size(t) >= sizeof(*t));
    assert(id > 0);
    assert(id < t->max_entries);

    /* remove it from the "allocated" list */

    struct rubber_object *o = &t->entries[id];
    assert(o->allocated);

    if (o->next != 0) {
        assert(id != t->allocated_tail);

        struct rubber_object *next = &t->entries[o->next];
        assert(next->allocated);
        assert(next->offset > o->offset);
        assert(next->previous == id);

        next->previous = o->previous;
    } else {
        assert(id == t->allocated_tail);

        t->allocated_tail = o->previous;
    }

    const unsigned previous_id = o->previous;
    struct rubber_object *previous = &t->entries[previous_id];
    assert(previous->allocated);
    assert(previous->offset < o->offset);
    assert(previous->next == id);

    previous->next = o->next;

    /* add it to the "free" list */

    o->next = t->free_head;
    t->free_head = id;

#ifndef NDEBUG
    o->allocated = false;
#endif

    return o->size;
}

static size_t
rubber_table_offset(const struct rubber_table *t, unsigned id)
{
    assert(t != NULL);
    assert(rubber_table_size(t) >= sizeof(*t));
    assert(id > 0);
    assert(id < t->max_entries);
    assert(id < t->initialized_tail);

    const struct rubber_object *o = &t->entries[id];
    assert(o->offset > 0);
    assert(o->offset >= rubber_table_size(t));
    assert(t->entries[o->previous].offset < o->offset);
    assert(o->next == 0 || t->entries[o->next].offset > o->offset);
    assert(o->next == 0 || t->entries[o->next].offset >= o->offset + o->size);

    return o->offset;
}

/*
 * utilities
 *
 */

static void *
rubber_write_at(struct rubber *r, size_t offset)
{
    assert(offset <= r->max_size);

    return ((uint8_t *)r->table) + offset;
}

static const void *
rubber_read_at(const struct rubber *r, size_t offset)
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
rubber_total_hole_list_size(const struct list_head *holes)
{
    size_t result = 0;
    for (const struct rubber_hole *hole = (const struct rubber_hole *)holes->next;
         &hole->siblings != holes;
         hole = (const struct rubber_hole *)hole->siblings.next) {
        assert(hole->siblings.prev->next == &hole->siblings);
        assert(hole->siblings.next->prev == &hole->siblings);
        assert(hole->size > 0);

        result += hole->size;
    }

    return result;
}

gcc_pure
static size_t
rubber_total_hole_size(const struct rubber *r)
{
    size_t result = 0;

    for (unsigned i = 0; i < N_RUBBER_HOLE_THRESHOLDS; ++i)
        result += rubber_total_hole_list_size(&r->holes[i]);

    return result;
}

#endif

static size_t
rubber_hole_offset(const struct rubber *r, const struct rubber_hole *hole)
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

static struct rubber_hole *
rubber_find_hole2(struct list_head *holes, size_t size)
{
    assert(size >= RUBBER_ALIGN);

    for (struct rubber_hole *h = (struct rubber_hole *)holes->next;
         &h->siblings != holes; h = (struct rubber_hole *)h->siblings.next)
        if (h->size >= size)
            return h;

    return NULL;
}

static struct rubber_hole *
rubber_find_hole(struct rubber *r, size_t size)
{
    struct list_head *holes = &r->holes[rubber_hole_threshold_lookup(size)];

    struct rubber_hole *h = rubber_find_hole2(holes, size);
    if (h == NULL) {
        while (holes > r->holes) {
            --holes;
            if (!list_empty(holes)) {
                h = (struct rubber_hole *)holes->next;
                break;
            }
        }
    }

    return h;
}

static void
rubber_hole_list_add(struct rubber *r, struct rubber_hole *hole)
{
    list_add(&hole->siblings,
             &r->holes[rubber_hole_threshold_lookup(hole->size)]);
}

static void
rubber_add_hole_after(struct rubber *r, unsigned reference_id,
                      size_t offset, size_t size)
{
    const struct rubber_table *const t = r->table;
    const struct rubber_object *const o = &t->entries[reference_id];
    assert(o->allocated);
    assert(o->next != 0);

    const unsigned next_id = o->next;
    const struct rubber_object *const next = &t->entries[next_id];
    assert(next->allocated);
    assert(next->offset > offset);
    assert(next->offset >= offset + size);

    size_t reference_end = o->offset + o->size;

    assert(offset >= reference_end);

    if (offset > reference_end) {
        /* follows an existing hole: grow the existing one */
        struct rubber_hole *hole = rubber_write_at(r, reference_end);
        assert(hole->siblings.prev->next == &hole->siblings);
        assert(hole->siblings.next->prev == &hole->siblings);
        assert(reference_end + hole->size == offset);
        assert(hole->previous_id == reference_id);

        list_remove(&hole->siblings);

        hole->size += size;
        hole->next_id = next_id;

        if (reference_end + hole->size < next->offset) {
            /* there's another hole to merge with */
            struct rubber_hole *next_hole =
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
        struct rubber_hole *next_hole = rubber_write_at(r, offset + size);
        assert(next_hole->siblings.prev->next == &next_hole->siblings);
        assert(next_hole->siblings.next->prev == &next_hole->siblings);
        assert(offset + size + next_hole->size == next->offset);
        assert(next_hole->next_id == next_id);

        list_remove(&next_hole->siblings);

        struct rubber_hole *hole = rubber_write_at(r, offset);
        hole->size = size + next_hole->size;
        hole->previous_id = reference_id;
        hole->next_id = next_id;

        rubber_hole_list_add(r, hole);
    } else {
        /* no existing hole before or after the new one */
        struct rubber_hole *hole = rubber_write_at(r, offset);
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

struct rubber *
rubber_new(size_t size)
{
    assert(RUBBER_ALIGN >= sizeof(struct rubber_hole));

    size = mmap_huge_page_size() + align_page_size(size);
    assert(size > sizeof(struct rubber));

    struct rubber *r = malloc(sizeof(*r));
    if (r == NULL)
        return NULL;

    void *p = mmap_alloc_anonymous(size);
    if (p == (void *)-1) {
        free(r);
        return NULL;
    }

    r->max_size = size;
    r->table = p;

    for (unsigned i = 0; i < N_RUBBER_HOLE_THRESHOLDS; ++i)
        list_init(&r->holes[i]);

    const size_t table_size = rubber_table_init(r->table, size / 1024);
    r->netto_size = 0;

    mmap_enable_huge_pages(rubber_write_at(r, table_size),
                           align_page_size_down(size - table_size));

    return r;
}

void
rubber_free(struct rubber *r)
{
    assert(rubber_table_is_empty(r->table));
    assert(r->netto_size == 0);

    rubber_table_deinit(r->table);
    mmap_free(r->table, r->max_size);
    free(r);
}

void
rubber_fork_cow(struct rubber *r, bool inherit)
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
rubber_add_in_hole(struct rubber *r, size_t size)
{
    assert(r != NULL);

    struct rubber_hole *hole = rubber_find_hole(r, size);
    if (hole == NULL)
        /* no hole found */
        return 0;

    /* found a hole */

    const unsigned previous_id = hole->previous_id;
    const unsigned next_id = hole->next_id;

    unsigned id = rubber_table_add_id(r->table);
    if (id == 0)
        return 0;

    struct rubber_object *const o = &r->table->entries[id];
    *o = (struct rubber_object){
        .next = next_id,
        .previous = previous_id,
        .offset = rubber_hole_offset(r, hole),
        .size = size,
#ifndef NDEBUG
        .allocated = true,
#endif
    };

    struct rubber_object *const previous = &r->table->entries[previous_id];
    struct rubber_object *const next = &r->table->entries[next_id];

    assert(previous->next == next_id);
    assert(next->previous == previous_id);

    previous->next = id;
    next->previous = id;

    list_remove(&hole->siblings);

    if (size != hole->size) {
        /* shrink the hole */

        struct rubber_hole *new_hole = (struct rubber_hole *)
            ((uint8_t *)hole + size);

        new_hole->size = hole->size - size;
        new_hole->previous_id = id;
        new_hole->next_id = next_id;

        rubber_hole_list_add(r, new_hole);
    }

    r->netto_size += size;

    return id;
}

unsigned
rubber_add(struct rubber *r, size_t size)
{
    assert(r != NULL);
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

    size_t offset = rubber_table_tail_offset(r->table);
    if (offset + size > r->max_size) {
        /* compress, then try again */
        rubber_compress(r);

        offset = rubber_table_tail_offset(r->table);
        if (offset + size > r->max_size)
            /* no, sorry, there's simply not enough free memory */
            return 0;
    }

    const unsigned id = rubber_table_add(r->table, offset, size);
    if (id > 0) {
        r->netto_size += size;
    }

    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));

    return id;
}

size_t
rubber_size_of(const struct rubber *r, unsigned id)
{
    assert(r != NULL);
    assert(id > 0);

    return rubber_table_size_of(r->table, id);
}

void *
rubber_write(struct rubber *r, unsigned id)
{
    const size_t offset = rubber_table_offset(r->table, id);
    assert(offset < r->max_size);
    return rubber_write_at(r, offset);
}

const void *
rubber_read(const struct rubber *r, unsigned id)
{
    const size_t offset = rubber_table_offset(r->table, id);
    assert(offset < r->max_size);
    return rubber_read_at(r, offset);
}

void
rubber_shrink(struct rubber *r, unsigned id, size_t new_size)
{
    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));
    assert(new_size > 0);

    struct rubber_object *const o = &r->table->entries[id];
    assert(o->allocated);
    assert(new_size <= o->size);

    new_size = align_size(new_size);

    if (new_size == o->size)
        return;

    const size_t hole_offset = o->offset + new_size;
    const size_t hole_size = o->size - new_size;

    size_t delta = rubber_table_shrink(r->table, id, new_size);
    r->netto_size -= delta;

    if (o->next != 0)
        rubber_add_hole_after(r, id, hole_offset, hole_size);

    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));
}

void
rubber_remove(struct rubber *r, unsigned id)
{
    assert(r->netto_size + rubber_total_hole_size(r) == rubber_get_brutto_size(r));
    assert(id > 0);

    struct rubber_object *const o = &r->table->entries[id];
    assert(o->allocated);

    const unsigned previous_id = o->previous;
    const unsigned next_id = o->next;

    size_t size = rubber_table_remove(r->table, id);
    assert(r->netto_size >= size);

    r->netto_size -= size;

    if (next_id == 0) {
        /* this is the last allocation */

        /* remove the hole before this */
        struct rubber_object *previous = &r->table->entries[previous_id];
        assert(previous->offset + previous->size <= o->offset);

        if (previous->offset + previous->size < o->offset) {
            struct rubber_hole *hole =
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
rubber_get_max_size(const struct rubber *r)
{
    return r->max_size - rubber_table_size(r->table);
}

size_t
rubber_get_brutto_size(const struct rubber *r)
{
    return rubber_table_get_brutto_size(r->table);
}

size_t
rubber_get_netto_size(const struct rubber *r)
{
    return r->netto_size;
}

static void
rubber_relocate(struct rubber *r, struct rubber_object *o, size_t offset)
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
rubber_compress(struct rubber *r)
{
    assert(r != NULL);

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

    struct rubber_object *o = rubber_table_head(r->table);
    assert(o->offset == 0);
    size_t offset = o->size;

    while ((o = rubber_table_next(r->table, o)) != NULL) {
        rubber_relocate(r, o, offset);
        offset += o->size;
    }

    assert(offset == r->netto_size + rubber_table_size(r->table));
    assert(r->netto_size == rubber_get_brutto_size(r));

    /* tell the kernel that we won't need the data after our last
       allocation */
    const size_t allocated = align_page_size(offset);
    if (allocated < r->max_size)
        mmap_discard_pages(rubber_write_at(r, allocated),
                           r->max_size - allocated);
}
