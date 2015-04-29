/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_DCHUNK_HXX
#define SHM_DCHUNK_HXX

#include "shm.h"
#include "util/Cast.hxx"

#include <inline/list.h>

#include <assert.h>
#include <stddef.h>

struct dpool_data {
    unsigned char data[sizeof(size_t)];
};

struct dpool_allocation {
    struct list_head all_siblings, free_siblings;

    struct dpool_data data;

    static constexpr struct dpool_allocation &FromPointer(const void *p) {
        return ContainerCast2(*(struct dpool_data *)const_cast<void *>(p),
                              &dpool_allocation::data);
    }
};

struct dpool_chunk {
    struct list_head siblings;
    size_t size, used;

    struct list_head all_allocations, free_allocations;

    struct dpool_data data;
};

static inline bool
dpool_chunk_contains(const struct dpool_chunk *chunk, const void *p)
{
    return (const unsigned char*)p >= chunk->data.data &&
        (const unsigned char*)p < chunk->data.data + chunk->used;
}

static inline struct dpool_allocation *
dpool_free_to_alloc(struct list_head *list)
{
    return &ContainerCast2(*list, &dpool_allocation::free_siblings);
}

static inline struct dpool_allocation *
dalloc_prev_free(struct dpool_allocation *alloc)
{
    return dpool_free_to_alloc(alloc->free_siblings.prev);
}

static inline struct dpool_allocation *
dalloc_next_free(struct dpool_allocation *alloc)
{
    return dpool_free_to_alloc(alloc->free_siblings.next);
}

static inline struct dpool_chunk *
dchunk_new(struct shm *shm, struct list_head *chunks_head)
{
    assert(shm != nullptr);
    assert(shm_page_size(shm) >= sizeof(struct dpool_chunk));

    auto *chunk = NewFromShm<struct dpool_chunk>(shm, 1);
    if (chunk == nullptr)
        return nullptr;

    chunk->size = shm_page_size(shm) - sizeof(*chunk) + sizeof(chunk->data);
    chunk->used = 0;

    list_init(&chunk->all_allocations);
    list_init(&chunk->free_allocations);

    list_add(&chunk->siblings, chunks_head);
    return chunk;
}

static inline void
dchunk_free(struct shm *shm, struct dpool_chunk *chunk)
{
    DeleteFromShm(shm, chunk);
}

#endif
