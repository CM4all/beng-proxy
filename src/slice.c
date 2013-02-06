/*
 * The "slice" memory allocator.  It is an allocator for large numbers
 * of small fixed-size objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "slice.h"

#include <inline/list.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

static const unsigned ALLOCATED = -1;
static const unsigned END_OF_LIST = -2;

#ifndef NDEBUG
static const unsigned MARK = -3;
#endif

struct slice_slot {
    unsigned next;
};

struct slice_area {
    struct list_head siblings;

    unsigned allocated_count;

    unsigned free_head;

    struct slice_slot slices[1];
};

struct slice_pool {
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

    struct list_head areas;
};

#define PAGE_SIZE 0x1000

gcc_const
static inline size_t
align_size(size_t size)
{
    return ((size - 1) | 0x1f) + 1;
}

gcc_const
static inline size_t
align_page_size(size_t size)
{
    return ((size - 1) | (PAGE_SIZE - 1)) + 1;
}

gcc_const
static unsigned
divide_round_up(unsigned a, unsigned b)
{
    assert(b > 0);

    return (a + b - 1) / b;
}

/*
 * slice_slot methods
 *
 */

static bool
slice_slot_is_allocated(const struct slice_slot *slot)
{
    return slot->next == ALLOCATED;
}

/*
 * slice_area methods
 *
 */

static bool
slice_area_is_full(gcc_unused const struct slice_pool *pool,
                   const struct slice_area *area)
{
    assert(area->free_head < pool->slices_per_area ||
           area->free_head == END_OF_LIST);

    return area->free_head == END_OF_LIST;
}

static bool
slice_area_is_empty(const struct slice_area *area)
{
    return area->allocated_count == 0;
}

static struct slice_area *
slice_area_new(struct slice_pool *pool)
{
    int flags = MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE;
    void *p = mmap(NULL, pool->area_size,
                   PROT_READ|PROT_WRITE, flags,
                   -1, 0);
    if (p == (void *)-1) {
        fputs("Out of adress space\n", stderr);
        abort();
    }

    struct slice_area *area = p;
    area->allocated_count = 0;
    area->free_head = 0;

    /* build the "free" list */
    for (unsigned i = 0; i < pool->slices_per_area - 1; ++i)
        area->slices[i].next = i + 1;
    area->slices[pool->slices_per_area - 1].next = END_OF_LIST;

    return area;
}

static void
slice_area_free(struct slice_pool *pool, struct slice_area *area)
{
    assert(area->allocated_count == 0);

#ifndef NDEBUG
    for (unsigned i = 0; i < pool->slices_per_area; ++i)
        assert(area->slices[i].next < pool->slices_per_area ||
               area->slices[i].next == END_OF_LIST);

    unsigned i = area->free_head;
    while (i != END_OF_LIST) {
        assert(i < pool->slices_per_area);

        unsigned next = area->slices[i].next;
        area->slices[i].next = MARK;
        i = next;
    }
#endif

    munmap(area, pool->area_size);
}

gcc_pure
static void *
slice_area_get_page(const struct slice_pool *pool, struct slice_area *area,
                    unsigned page)
{
    assert(page <= pool->pages_per_area);

    return (uint8_t *)area + (pool->header_pages + page) * PAGE_SIZE;
}

gcc_pure
static void *
slice_area_get_slice(const struct slice_pool *pool, struct slice_area *area,
                     unsigned slice)
{
    assert(slice < pool->slices_per_area);
    assert(slice_slot_is_allocated(&area->slices[slice]));

    unsigned page = (slice / pool->slices_per_page) * pool->pages_per_slice;
    slice %= pool->slices_per_page;

    return (uint8_t *)slice_area_get_page(pool, area, page)
        + slice * pool->slice_size;
}

/**
 * Calculates the allocation slot index from an allocated pointer.
 * This is used to locate the #slice_slot for a pointer passed to a
 * public function.
 */
gcc_pure
static unsigned
slice_area_index(const struct slice_pool *pool, struct slice_area *area,
                 const void *_p)
{
    const uint8_t *p = _p;
    assert(p >= (uint8_t *)slice_area_get_page(pool, area, 0));
    assert(p < (uint8_t *)slice_area_get_page(pool, area,
                                              pool->pages_per_area));

    size_t offset = p - (const uint8_t *)area;
    const unsigned page = offset / PAGE_SIZE - pool->header_pages;
    offset %= PAGE_SIZE;
    assert(offset % pool->slice_size == 0);

    return page * pool->slices_per_page / pool->pages_per_slice
        + offset / pool->slice_size;
}

/**
 * Find the first free slot index, starting at the specified position.
 */
gcc_pure
static unsigned
slice_area_find_free(const struct slice_pool *pool,
                     const struct slice_area *area, unsigned start)
{
    assert(start <= pool->slices_per_page);

    const unsigned end = pool->slices_per_page;
    const struct slice_slot *slices = area->slices;

    unsigned i;
    for (i = start; i != end; ++i)
        if (!slice_slot_is_allocated(&slices[i]))
            break;

    return i;
}

/**
 * Find the first allocated slot index, starting at the specified
 * position.
 */
gcc_pure
static unsigned
slice_area_find_allocated(const struct slice_pool *pool,
                          const struct slice_area *area, unsigned start)
{
    assert(start <= pool->slices_per_page);

    const unsigned end = pool->slices_per_page;
    const struct slice_slot *slices = area->slices;

    unsigned i;
    for (i = start; i != end; ++i)
        if (slice_slot_is_allocated(&slices[i]))
            break;

    return i;
}

/**
 * Punch a hole in the memory map in the specified slot index range.
 * This means notifying the kernel that we will no longer need the
 * contents, which allows the kernel to drop the allocated pages and
 * reuse it for other processes.
 */
static void
slice_area_punch_slice_range(struct slice_pool *pool, struct slice_area *area,
                             unsigned start, unsigned end)
{
    assert(start <= end);

    unsigned start_page = divide_round_up(start, pool->slices_per_page)
        * pool->pages_per_slice;
    unsigned end_page = (start / pool->slices_per_page)
        * pool->pages_per_slice;
    assert(start_page <= end_page);
    if (start_page == end_page)
        return;

    uint8_t *start_pointer = slice_area_get_page(pool, area, start_page);
    uint8_t *end_pointer = slice_area_get_page(pool, area, end_page);

    madvise(start_pointer, end_pointer - start_pointer, MADV_DONTNEED);
}

static void
slice_area_compress(struct slice_pool *pool, struct slice_area *area)
{
    unsigned position = 0;

    while (true) {
        unsigned first_free = slice_area_find_free(pool, area, position);
        if (first_free == pool->slices_per_page)
            break;

        unsigned first_allocated =
            slice_area_find_allocated(pool, area, first_free + 1);
        slice_area_punch_slice_range(pool, area, first_free, first_allocated);

        position = first_allocated;
    }
}

/*
 * slice_pool methods
 *
 */

struct slice_pool *
slice_pool_new(size_t slice_size, unsigned slices_per_area)
{
    assert(slice_size > 0);
    assert(slices_per_area > 0);

    struct slice_pool *pool = malloc(sizeof(*pool));
    if (gcc_unlikely(pool == NULL)) {
        fputs("Out of memory\n", stderr);
        abort();
    }

    if (slice_size <= PAGE_SIZE / 2) {
        pool->slice_size = align_size(slice_size);

        pool->slices_per_page = PAGE_SIZE / pool->slice_size;
        pool->pages_per_slice = 1;

        pool->pages_per_area = divide_round_up(slices_per_area,
                                               pool->slices_per_page);
    } else {
        pool->slice_size = align_page_size(slice_size);

        pool->slices_per_page = 1;
        pool->pages_per_slice = pool->slice_size / PAGE_SIZE;

        pool->pages_per_area = slices_per_area * pool->pages_per_slice;
    }

    pool->slices_per_area = pool->pages_per_area * pool->slices_per_page;

    const struct slice_area *area = NULL;
    const size_t header_size = sizeof(*area)
        + sizeof(area->slices[0]) * (pool->slices_per_area - 1);
    pool->header_pages = divide_round_up(header_size, PAGE_SIZE);

    pool->area_size = PAGE_SIZE * (pool->header_pages + pool->pages_per_area);

    list_init(&pool->areas);
    return pool;
}

void
slice_pool_free(struct slice_pool *pool)
{
    while (!list_empty(&pool->areas)) {
        struct slice_area *area = (struct slice_area *)pool->areas.next;

        /* must be empty at this point, or it's a memory leak */
        assert(area->allocated_count == 0);

        list_remove(&area->siblings);
        slice_area_free(pool, area);
    }

    free(pool);
}

size_t
slice_pool_get_slice_size(const struct slice_pool *pool)
{
    assert(pool != NULL);

    return pool->slice_size;
}

void
slice_pool_compress(struct slice_pool *pool)
{
    for (struct slice_area *area = (struct slice_area *)pool->areas.next,
             *next = (struct slice_area *)area->siblings.next;
         &area->siblings != &pool->areas;
         area = next, next = (struct slice_area *)area->siblings.next) {
        if (slice_area_is_empty(area)) {
            list_remove(&area->siblings);
            slice_area_free(pool, area);
        } else
            slice_area_compress(pool, area);
    }
}

gcc_pure
static struct slice_area *
slice_pool_find_non_full_area(struct slice_pool *pool)
{
    assert(pool != NULL);

    for (struct slice_area *area = (struct slice_area *)pool->areas.next;
         &area->siblings != &pool->areas;
         area = (struct slice_area *)area->siblings.next)
        if (!slice_area_is_full(pool, area))
            return area;

    return NULL;
}

struct slice_area *
slice_pool_get_area(struct slice_pool *pool)

{
    assert(pool != NULL);

    struct slice_area *area = slice_pool_find_non_full_area(pool);
    if (area == NULL) {
        area = slice_area_new(pool);
        list_add(&area->siblings, &pool->areas);
    }

    return area;
}

void *
slice_alloc(struct slice_pool *pool, struct slice_area *area)
{
    assert(pool != NULL);
    assert(area != NULL);
    assert(!slice_area_is_full(pool, area));

    const unsigned i = area->free_head;
    struct slice_slot *const slot = &area->slices[i];

    ++area->allocated_count;
    area->free_head = slot->next;
    slot->next = ALLOCATED;

    return slice_area_get_slice(pool, area, i);
}

void
slice_free(struct slice_pool *pool, struct slice_area *area, void *p)
{
    unsigned i = slice_area_index(pool, area, p);
    assert(slice_slot_is_allocated(&area->slices[i]));

    area->slices[i].next = area->free_head;
    area->free_head = i;

    assert(area->allocated_count > 0);
    --area->allocated_count;
}
