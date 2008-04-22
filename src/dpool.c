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
    struct dpool *pool = shm_alloc(shm);
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

static void *
allocation_alloc(struct dpool_allocation *alloc, size_t size __attr_unused)
{
    /* XXX split allocation */

    list_remove(&alloc->free_siblings);
    return alloc->data;
}

static void *
dchunk_malloc(struct dpool_chunk *chunk, size_t size)
{
    struct dpool_allocation *alloc;

    for (alloc = (struct dpool_allocation *)chunk->free_allocations.next;
         alloc != (struct dpool_allocation *)&chunk->free_allocations;
         alloc = (struct dpool_allocation *)alloc->free_siblings.next) {
        if (allocation_size(chunk, alloc) >= size)
            return allocation_alloc(alloc, size);
    }

    if (sizeof(*alloc) - sizeof(alloc->data) + size > chunk->size - chunk->used)
        return NULL;

    alloc = (struct dpool_allocation *)(chunk->data + chunk->used);
    chunk->used += sizeof(*alloc) - sizeof(alloc->data) + size;

    list_add(&alloc->all_siblings, chunk->all_allocations.prev);

    return alloc->data;
}

static struct dpool_chunk *
dchunk_new(struct dpool *pool)
{
    struct dpool_chunk *chunk = shm_alloc(pool->shm);
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

    chunk = (struct dpool_chunk *)pool->first_chunk.siblings.next;
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

void
d_free(struct dpool *pool __attr_unused, const void *p __attr_unused)
{
    /* XXX implement this */
}
