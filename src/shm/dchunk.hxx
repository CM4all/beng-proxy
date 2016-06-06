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

struct DpoolData {
    unsigned char data[sizeof(size_t)];
};

struct DpoolAllocation {
    struct list_head all_siblings, free_siblings;

    DpoolData data;

    static constexpr DpoolAllocation &FromPointer(const void *p) {
        return ContainerCast2(*(DpoolData *)const_cast<void *>(p),
                              &DpoolAllocation::data);
    }

    static constexpr DpoolAllocation *FromFreeHead(struct list_head *list) {
        return &ContainerCast2(*list, &DpoolAllocation::free_siblings);
    }

    constexpr DpoolAllocation *GetPreviousFree() {
        return FromFreeHead(free_siblings.prev);
    }

    constexpr DpoolAllocation *GetNextFree() {
        return FromFreeHead(free_siblings.prev);
    }

    void MarkAllocated() {
        list_init(&free_siblings);
    }

    bool IsAllocated() {
        return list_empty(&free_siblings);
    }
};

struct DpoolChunk {
    struct list_head siblings;
    const size_t size;
    size_t used = 0;

    struct list_head all_allocations, free_allocations;

    DpoolData data;

    DpoolChunk(size_t _size)
        :size(_size) {
        list_init(&all_allocations);
        list_init(&free_allocations);
    }

    static DpoolChunk *New(struct shm &shm, struct list_head &chunks_head) {
        assert(shm_page_size(&shm) >= sizeof(DpoolChunk));

        DpoolChunk *chunk;
        const size_t size = shm_page_size(&shm) - sizeof(*chunk) + sizeof(chunk->data);
        chunk = NewFromShm<DpoolChunk>(&shm, 1, size);
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
