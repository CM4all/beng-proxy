/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_DCHUNK_H
#define __BENG_DCHUNK_H

#include "shm.h"

#include <inline/list.h>

#include <assert.h>
#include <stddef.h>
#include <stdbool.h>

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

static inline bool
dpool_chunk_contains(const struct dpool_chunk *chunk, const void *p)
{
    return (const unsigned char*)p >= chunk->data &&
        (const unsigned char*)p < chunk->data + chunk->used;
}

static inline struct dpool_allocation *
dpool_free_to_alloc(struct list_head *list)
{
    return (struct dpool_allocation *)(((char*)list) - offsetof(struct dpool_allocation, free_siblings));
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
    struct dpool_chunk *chunk;

    assert(shm != NULL);
    assert(shm_page_size(shm) >= sizeof(*chunk));

    chunk = shm_alloc(shm, 1);
    if (chunk == NULL)
        return NULL;

    chunk->size = shm_page_size(shm) - sizeof(*chunk) + sizeof(chunk->data);
    chunk->used = 0;

    list_init(&chunk->all_allocations);
    list_init(&chunk->free_allocations);

    list_add(&chunk->siblings, chunks_head);
    return chunk;
}

#endif
