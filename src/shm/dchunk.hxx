/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_DCHUNK_HXX
#define SHM_DCHUNK_HXX

#include "shm.hxx"
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

    static constexpr struct dpool_allocation *FromFreeHead(struct list_head *list) {
        return &ContainerCast2(*list, &dpool_allocation::free_siblings);
    }

    constexpr struct dpool_allocation *GetPreviousFree() {
        return FromFreeHead(free_siblings.prev);
    }

    constexpr struct dpool_allocation *GetNextFree() {
        return FromFreeHead(free_siblings.prev);
    }
};

struct dpool_chunk {
    struct list_head siblings;
    const size_t size;
    size_t used = 0;

    struct list_head all_allocations, free_allocations;

    struct dpool_data data;

    dpool_chunk(size_t _size)
        :size(_size) {
        list_init(&all_allocations);
        list_init(&free_allocations);
    }

    static struct dpool_chunk *New(struct shm &shm,
                                   struct list_head &chunks_head) {
        assert(shm_page_size(&shm) >= sizeof(struct dpool_chunk));

        struct dpool_chunk *chunk;
        const size_t size = shm_page_size(&shm) - sizeof(*chunk) + sizeof(chunk->data);
        chunk = NewFromShm<struct dpool_chunk>(&shm, 1, size);
        if (chunk == nullptr)
            return nullptr;

        list_add(&chunk->siblings, &chunks_head);
        return chunk;
    }

    void Destroy(struct shm &shm) {
        DeleteFromShm(&shm, this);
    }

    gcc_pure
    bool Contains(const void *p) const {
        return (const unsigned char *)p >= data.data &&
            (const unsigned char *)p < data.data + used;
    }
};

#endif
