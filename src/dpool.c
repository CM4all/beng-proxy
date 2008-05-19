/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "dpool.h"
#include "shm.h"
#include "lock.h"

#include <inline/list.h>
#include <inline/compiler.h>
#include <inline/poison.h>

#include <assert.h>

#if defined(__x86_64__) || defined(__PPC64__)
#define ALIGN 8
#define ALIGN_BITS 0x7
#else
#define ALIGN 4
#define ALIGN_BITS 0x3
#endif

struct dpool_allocation {
    struct list_head all_siblings, free_siblings;

    unsigned char data[sizeof(size_t)];
};

struct dpool_chunk {
    struct list_head siblings;
    size_t size, used;

    struct list_head all_allocations, free_allocations;

    unsigned char data[sizeof(size_t)];
};

struct dpool {
    struct shm *shm;
    struct lock lock;
    struct dpool_chunk first_chunk;
};

static inline size_t __attr_const
align_size(size_t size)
{
    return ((size - 1) | ALIGN_BITS) + 1;
}

struct dpool *
dpool_new(struct shm *shm)
{
    struct dpool *pool = shm_alloc(shm, 1);
    if (pool == NULL)
        return NULL;

    assert(shm_page_size(shm) >= sizeof(*pool));

    pool->shm = shm;
    lock_init(&pool->lock);

    list_init(&pool->first_chunk.siblings);
    pool->first_chunk.size = shm_page_size(shm) - sizeof(*pool) +
        sizeof(pool->first_chunk.data);
    pool->first_chunk.used = 0;

    list_init(&pool->first_chunk.all_allocations);
    list_init(&pool->first_chunk.free_allocations);

    return pool;
}

void
dpool_destroy(struct dpool *pool)
{
    struct dpool_chunk *chunk, *n;

    assert(pool != NULL);
    assert(pool->shm != NULL);
    assert(pool->first_chunk.size == shm_page_size(pool->shm) - sizeof(*pool) +
           sizeof(pool->first_chunk.data));

    for (chunk = (struct dpool_chunk *)pool->first_chunk.siblings.next;
         chunk != &pool->first_chunk; chunk = n) {
        n = (struct dpool_chunk *)chunk->siblings.next;

        shm_free(pool->shm, chunk);
    }

    lock_destroy(&pool->lock);

    shm_free(pool->shm, pool);
}

static size_t
allocation_size(const struct dpool_chunk *chunk,
                const struct dpool_allocation *alloc)
{
    if (alloc->all_siblings.next == &chunk->all_allocations)
        return chunk->data + chunk->used - alloc->data;
    else
        return (const unsigned char *)alloc->all_siblings.next - alloc->data;
}

static void
allocation_split(const struct dpool_chunk *chunk __attr_unused,
                 struct dpool_allocation *alloc, size_t size)
{
    struct dpool_allocation *other;

    assert(allocation_size(chunk, alloc) > size + sizeof(*alloc) * 2);

    other = (struct dpool_allocation *)(alloc->data + size);
    list_add(&other->all_siblings, &alloc->all_siblings);
    list_add(&other->free_siblings, &alloc->free_siblings);
}

static void *
allocation_alloc(const struct dpool_chunk *chunk,
                 struct dpool_allocation *alloc,
                 size_t size)
{
    if (allocation_size(chunk, alloc) > size + sizeof(*alloc) * 2)
        allocation_split(chunk, alloc, size);

    assert(allocation_size(chunk, alloc) >= size);

    list_remove(&alloc->free_siblings);
    list_init(&alloc->free_siblings);
    return alloc->data;
}

static struct dpool_allocation *
dpool_free_to_alloc(struct list_head *list)
{
    return (struct dpool_allocation *)(((char*)list) - offsetof(struct dpool_allocation, free_siblings));
}

static void *
dchunk_malloc(struct dpool_chunk *chunk, size_t size)
{
    struct dpool_allocation *alloc;

    for (alloc = dpool_free_to_alloc(chunk->free_allocations.next);
         &alloc->free_siblings != &chunk->free_allocations;
         alloc = dpool_free_to_alloc(alloc->free_siblings.next)) {
        if (allocation_size(chunk, alloc) >= size)
            return allocation_alloc(chunk, alloc, size);
    }

    if (sizeof(*alloc) - sizeof(alloc->data) + size > chunk->size - chunk->used)
        return NULL;

    alloc = (struct dpool_allocation *)(chunk->data + chunk->used);
    chunk->used += sizeof(*alloc) - sizeof(alloc->data) + size;

    list_add(&alloc->all_siblings, chunk->all_allocations.prev);
    list_init(&alloc->free_siblings);

    return alloc->data;
}

static struct dpool_chunk *
dchunk_new(struct dpool *pool)
{
    struct dpool_chunk *chunk = shm_alloc(pool->shm, 1);
    if (chunk == NULL)
        return NULL;

    chunk->size = shm_page_size(pool->shm) - sizeof(*chunk) + sizeof(chunk->data);
    chunk->used = 0;

    list_init(&chunk->all_allocations);
    list_init(&chunk->free_allocations);

    list_add(&chunk->siblings, &pool->first_chunk.siblings);
    return chunk;
}

void *
d_malloc(struct dpool *pool, size_t size)
{
    struct dpool_chunk *chunk;
    void *p;

    assert(pool != NULL);
    assert(pool->shm != NULL);
    assert(pool->first_chunk.size == shm_page_size(pool->shm) - sizeof(*pool) +
           sizeof(pool->first_chunk.data));

    size = align_size(size);

    /* XXX allow multi-page chunks */
    if (size > pool->first_chunk.size)
        return NULL;

    lock_lock(&pool->lock);

    /* find a chunk with enough room */

    chunk = &pool->first_chunk;
    do {
        p = dchunk_malloc(chunk, size);
        if (p != NULL) {
            lock_unlock(&pool->lock);
            return p;
        }

        chunk = (struct dpool_chunk *)chunk->siblings.next;
    } while (chunk != &pool->first_chunk);

    /* none found; try to allocate a new chunk */

    assert(p == NULL);

    chunk = dchunk_new(pool);
    if (chunk != NULL) {
        p = dchunk_malloc(chunk, size);
        assert(p != NULL);
    }

    lock_unlock(&pool->lock);

    return p;
}

static struct dpool_allocation *
dpool_pointer_to_allocation(const void *p)
{
    union {
        const void *in;
        char *out;
    } u = { .in = p };

    return (struct dpool_allocation *)(u.out - offsetof(struct dpool_allocation, data));
}

static bool
dpool_chunk_contains(const struct dpool_chunk *chunk, const void *p)
{
    return (const unsigned char*)p >= chunk->data &&
        (const unsigned char*)p < chunk->data + chunk->used;
}

static struct dpool_chunk *
dpool_find_chunk(struct dpool *pool, const void *p)
{
    struct dpool_chunk *chunk;

    if (dpool_chunk_contains(&pool->first_chunk, p))
        return &pool->first_chunk;

    for (chunk = (struct dpool_chunk *)pool->first_chunk.siblings.next;
         chunk != &pool->first_chunk;
         chunk = (struct dpool_chunk *)chunk->siblings.next) {
        if (dpool_chunk_contains(chunk, p))
            return chunk;
    }

    return NULL;
}

static struct dpool_allocation *
dpool_find_free(const struct dpool_chunk *chunk,
                struct dpool_allocation *alloc)
{
    struct dpool_allocation *p;

    for (p = (struct dpool_allocation *)alloc->all_siblings.prev;
         p != (const struct dpool_allocation *)&chunk->all_allocations;
         p = (struct dpool_allocation *)p->all_siblings.prev)
        if (!list_empty(&p->free_siblings))
            return p;

    return NULL;
}

void
d_free(struct dpool *pool, const void *p)
{
    struct dpool_chunk *chunk = dpool_find_chunk(pool, p);
    struct dpool_allocation *alloc = dpool_pointer_to_allocation(p);
    struct dpool_allocation *prev, *next;

    assert(chunk != NULL);
    assert(list_empty(&alloc->free_siblings));    

    lock_lock(&pool->lock);

    prev = dpool_find_free(chunk, alloc);
    if (prev == NULL)
        list_add(&alloc->free_siblings, &chunk->free_allocations);
    else
        list_add(&alloc->free_siblings, &prev->free_siblings);

    prev = dpool_free_to_alloc(alloc->free_siblings.prev);
    if (&prev->free_siblings != &chunk->free_allocations &&
        prev == (struct dpool_allocation *)alloc->all_siblings.prev) {
        /* merge with previous */
        list_remove(&alloc->all_siblings);
        list_remove(&alloc->free_siblings);
        alloc = prev;
    }

    next = dpool_free_to_alloc(alloc->free_siblings.next);
    if (&next->free_siblings != &chunk->free_allocations &&
        next == (struct dpool_allocation *)alloc->all_siblings.next) {
        /* merge with next */
        list_remove(&next->all_siblings);
        list_remove(&next->free_siblings);
    }

    if (alloc->all_siblings.next == &chunk->all_allocations) {
        /* remove free tail allocation */
        assert(alloc->free_siblings.next == &chunk->free_allocations);
        list_remove(&alloc->all_siblings);
        list_remove(&alloc->free_siblings);
        chunk->used = (unsigned char*)alloc - chunk->data;
    }

    /* XXX merge */

    lock_unlock(&pool->lock);
}
