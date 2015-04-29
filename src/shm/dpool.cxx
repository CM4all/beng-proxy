/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "dpool.hxx"
#include "dchunk.hxx"
#include "shm.h"
#include "lock.h"

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

struct dpool {
    struct shm *const shm;
    struct lock lock;
    struct dpool_chunk first_chunk;

    explicit dpool(struct shm *_shm);
    ~dpool();
};

dpool::dpool(struct shm *_shm)
    :shm(_shm)
{
    assert(shm_page_size(shm) >= sizeof(*this));

    lock_init(&lock);
    list_init(&first_chunk.siblings);

    first_chunk.size = shm_page_size(shm) - sizeof(*this) +
        sizeof(first_chunk.data);
    first_chunk.used = 0;

    list_init(&first_chunk.all_allocations);
    list_init(&first_chunk.free_allocations);
}

dpool::~dpool()
{
    assert(shm != nullptr);
    assert(first_chunk.size == shm_page_size(shm) - sizeof(*this) +
           sizeof(first_chunk.data));

    struct dpool_chunk *chunk, *n;
    for (chunk = (struct dpool_chunk *)first_chunk.siblings.next;
         chunk != &first_chunk; chunk = n) {
        n = (struct dpool_chunk *)chunk->siblings.next;

        dchunk_free(shm, chunk);
    }

    lock_destroy(&lock);
}

static constexpr size_t
align_size(size_t size)
{
    return ((size - 1) | ALIGN_BITS) + 1;
}

struct dpool *
dpool_new(struct shm *shm)
{
    return NewFromShm<struct dpool>(shm, 1, shm);
}

void
dpool_destroy(struct dpool *pool)
{
    assert(pool != nullptr);

    DeleteFromShm(pool->shm, pool);
}

static size_t
allocation_size(const struct dpool_chunk *chunk,
                const struct dpool_allocation *alloc)
{
    if (alloc->all_siblings.next == &chunk->all_allocations)
        return chunk->data.data + chunk->used - alloc->data.data;
    else
        return (const unsigned char *)alloc->all_siblings.next - alloc->data.data;
}

bool
dpool_is_fragmented(const struct dpool *pool)
{
    size_t reserved = 0, freed = 0;
    const struct dpool_chunk *chunk = &pool->first_chunk;

    do {
        reserved += chunk->used;

        for (auto alloc = dpool_free_to_alloc(chunk->free_allocations.next);
             &alloc->free_siblings != &chunk->free_allocations;
             alloc = dpool_free_to_alloc(alloc->free_siblings.next))
            freed += allocation_size(chunk, alloc);

        chunk = (struct dpool_chunk *)chunk->siblings.next;
    } while (chunk != &pool->first_chunk);

    return reserved > 0 && freed * 4 > reserved;
}

static void
allocation_split(const struct dpool_chunk *chunk gcc_unused,
                 struct dpool_allocation *alloc, size_t size)
{
    assert(allocation_size(chunk, alloc) > size + sizeof(*alloc) * 2);

    auto *other = (struct dpool_allocation *)(void *)(alloc->data.data + size);
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
    return &alloc->data;
}

static void *
dchunk_malloc(struct dpool_chunk *chunk, size_t size)
{
    struct dpool_allocation *alloc;

    for (alloc = dpool_free_to_alloc(chunk->free_allocations.next);
         &alloc->free_siblings != &chunk->free_allocations;
         alloc = dalloc_next_free(alloc)) {
        if (allocation_size(chunk, alloc) >= size)
            return allocation_alloc(chunk, alloc, size);
    }

    if (sizeof(*alloc) - sizeof(alloc->data) + size > chunk->size - chunk->used)
        return nullptr;

    alloc = (struct dpool_allocation *)(void *)(chunk->data.data + chunk->used);
    chunk->used += sizeof(*alloc) - sizeof(alloc->data) + size;

    list_add(&alloc->all_siblings, chunk->all_allocations.prev);
    list_init(&alloc->free_siblings);

    return &alloc->data;
}

void *
d_malloc(struct dpool *pool, size_t size)
{
    void *p;

    assert(pool != nullptr);
    assert(pool->shm != nullptr);
    assert(pool->first_chunk.size == shm_page_size(pool->shm) - sizeof(*pool) +
           sizeof(pool->first_chunk.data));

    size = align_size(size);

    /* we could theoretically allow larger allocations by using
       multiple consecutive chunks, but we don't implement that
       because our current use cases should not need to allocate such
       large structures */
    if (size > pool->first_chunk.size)
        return nullptr;

    lock_lock(&pool->lock);

    /* find a chunk with enough room */

    auto *chunk = &pool->first_chunk;
    do {
        p = dchunk_malloc(chunk, size);
        if (p != nullptr) {
            lock_unlock(&pool->lock);
            return p;
        }

        chunk = (struct dpool_chunk *)chunk->siblings.next;
    } while (chunk != &pool->first_chunk);

    /* none found; try to allocate a new chunk */

    assert(p == nullptr);

    chunk = dchunk_new(pool->shm, &pool->first_chunk.siblings);
    if (chunk != nullptr) {
        p = dchunk_malloc(chunk, size);
        assert(p != nullptr);
    }

    lock_unlock(&pool->lock);

    return p;
}

static struct dpool_chunk *
dpool_find_chunk(struct dpool *pool, const void *p)
{
    if (dpool_chunk_contains(&pool->first_chunk, p))
        return &pool->first_chunk;

    for (auto *chunk = (struct dpool_chunk *)pool->first_chunk.siblings.next;
         chunk != &pool->first_chunk;
         chunk = (struct dpool_chunk *)chunk->siblings.next) {
        if (dpool_chunk_contains(chunk, p))
            return chunk;
    }

    return nullptr;
}

static struct dpool_allocation *
dpool_find_free(const struct dpool_chunk *chunk,
                struct dpool_allocation *alloc)
{
    for (auto *p = (struct dpool_allocation *)alloc->all_siblings.prev;
         p != (const struct dpool_allocation *)&chunk->all_allocations;
         p = (struct dpool_allocation *)p->all_siblings.prev)
        if (!list_empty(&p->free_siblings))
            return p;

    return nullptr;
}

void
d_free(struct dpool *pool, const void *p)
{
    struct dpool_chunk *chunk = dpool_find_chunk(pool, p);
    auto *alloc = &dpool_allocation::FromPointer(p);

    assert(chunk != nullptr);
    assert(list_empty(&alloc->free_siblings));

    lock_lock(&pool->lock);

    auto *prev = dpool_find_free(chunk, alloc);
    if (prev == nullptr)
        list_add(&alloc->free_siblings, &chunk->free_allocations);
    else
        list_add(&alloc->free_siblings, &prev->free_siblings);

    prev = dalloc_prev_free(alloc);
    if (&prev->free_siblings != &chunk->free_allocations &&
        prev == (struct dpool_allocation *)alloc->all_siblings.prev) {
        /* merge with previous */
        list_remove(&alloc->all_siblings);
        list_remove(&alloc->free_siblings);
        alloc = prev;
    }

    auto *next = dalloc_next_free(alloc);
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
        chunk->used = (unsigned char*)alloc - chunk->data.data;

        if (chunk->used == 0 && chunk != &pool->first_chunk) {
            /* the chunk is completely empty; release it to the SHM
               object */
            assert(list_empty(&chunk->all_allocations));
            assert(list_empty(&chunk->free_allocations));

            list_remove(&chunk->siblings);
            dchunk_free(pool->shm, chunk);
        }
    }

    lock_unlock(&pool->lock);
}
