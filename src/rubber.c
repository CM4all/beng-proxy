/*
 * The "rubber" memory allocator.  It is a buffer for storing many
 * large objects.  Unlike heap memory, unused areas are given back to
 * the operating system.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rubber.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

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

struct rubber {
    /**
     * The maximum size of the memory map.  This is the value passed
     * to rubber_new() and will never be changed.
     */
    size_t max_size;

    /**
     * The offset of the first free byte after the last allocation.
     * This is the amount of physical memory allocated by the kernel
     * for our memory map.
     */
    size_t brutto_size;

    /**
     * The sum of all allocation sizes.
     */
    size_t netto_size;

    /**
     * The table managing the allocations in the memory map.  At the
     * same time, this is the pointer to the memory map.
     */
    struct rubber_table *table;
};

gcc_const
static inline size_t
align_page_size(size_t size)
{
    return ((size - 1) | 0x3fffff) + 1;
}

gcc_const
static inline size_t
align_size(size_t size)
{
    return ((size - 1) | 0x1f) + 1;
}

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

    /* round to nearest "huge page", so the first real allocation
       starts at a "huge page" boundary */
    const size_t table_size =
        align_page_size(rubber_table_required_size(max_entries));

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

static unsigned
rubber_table_add(struct rubber_table *t, size_t offset, size_t size)
{
    assert(t != NULL);
    assert(t->allocated_tail < t->max_entries);

    unsigned id;

    if (t->free_head == 0) {
        if (t->initialized_tail >= t->max_entries)
            /* no more entries in the table (though there may still be
               enough space in the memory map) */
            return 0;

        id = t->initialized_tail++;
    } else {

        /* remove the first item from the "free" list .. */

        id = t->free_head;
        struct rubber_object *o = &t->entries[id];
        assert(!o->allocated);
        t->free_head = o->next;
    }

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

    struct rubber_object *previous = &t->entries[o->previous];
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

struct rubber *
rubber_new(size_t size)
{
    size = align_page_size(1) + align_page_size(size);
    assert(size > sizeof(struct rubber));

    struct rubber *r = malloc(sizeof(*r));
    if (r == NULL)
        return NULL;

    int flags = MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE;

    void *p = mmap(NULL, size,
                   PROT_READ|PROT_WRITE, flags,
                   -1, 0);
    if (p == (void *)-1) {
        free(r);
        return NULL;
    }

    r->max_size = size;
    r->table = p;

    const size_t table_size = rubber_table_init(r->table, size / 1024);
    r->brutto_size = r->netto_size = 0;

#ifdef MADV_HUGEPAGE
    /* allow the Linux kernel to use "Huge Pages" for the cache, which
       reduces page table overhead for this big chunk of data */
    madvise(rubber_write_at(r, table_size), size - table_size, MADV_HUGEPAGE);
#endif

    return r;
}

void
rubber_free(struct rubber *r)
{
    assert(rubber_table_is_empty(r->table));
    assert(r->netto_size == 0);

    rubber_table_deinit(r->table);
    munmap(r->table, r->max_size);
    free(r);
}

unsigned
rubber_add(struct rubber *r, size_t size)
{
    assert(r != NULL);
    assert(size > 0);

    if (size >= r->max_size)
        /* sanity check to avoid integer overflows */
        return 0;

    if (r->brutto_size / 3 >= r->netto_size)
        /* auto-compress when a lot of allocations have been freed */
        rubber_compress(r);

    size = align_size(size);

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
        r->brutto_size += size;
        r->netto_size += size;
    }

    return id;
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
    r->netto_size -= rubber_table_shrink(r->table, id, align_size(new_size));
}

void
rubber_remove(struct rubber *r, unsigned id)
{
    size_t size = rubber_table_remove(r->table, id);
    assert(r->netto_size >= size);

    r->netto_size -= size;
}

size_t
rubber_get_brutto_size(const struct rubber *r)
{
    return r->brutto_size;
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

    assert(r->brutto_size >= r->netto_size);

    if (r->brutto_size == r->netto_size)
        return;

    /* relocate all items, eliminate spaces */

    struct rubber_object *o = rubber_table_head(r->table);
    assert(o->offset == 0);
    size_t offset = o->size;

    while ((o = rubber_table_next(r->table, o)) != NULL) {
        rubber_relocate(r, o, offset);
        offset += o->size;
    }

    assert(offset == r->netto_size + rubber_table_size(r->table));
    r->brutto_size = offset - rubber_table_size(r->table);

    /* tell the kernel that we won't need the data after our last
       allocation */
    const size_t allocated = align_page_size(offset);
    if (allocated < r->max_size)
        madvise(rubber_write_at(r, allocated), r->max_size - allocated,
                MADV_DONTNEED);
}
