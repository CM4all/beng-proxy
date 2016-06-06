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

    gcc_pure
    size_t GetAllocationSize(const DpoolAllocation &alloc) const {
        if (alloc.all_siblings.next == &all_allocations)
            return data.data + used - alloc.data.data;
        else
            return (const unsigned char *)alloc.all_siblings.next - alloc.data.data;
    }

    void Split(DpoolAllocation &alloc, size_t alloc_size) {
        assert(GetAllocationSize(alloc) > alloc_size + sizeof(alloc) * 2);

        auto &other = *(DpoolAllocation *)(void *)(alloc.data.data + alloc_size);
        list_add(&other.all_siblings, &alloc.all_siblings);
        list_add(&other.free_siblings, &alloc.free_siblings);
    }

    void *Allocate(DpoolAllocation &alloc, size_t alloc_size) {
        if (GetAllocationSize(alloc) > alloc_size + sizeof(alloc) * 2)
            Split(alloc, alloc_size);

        assert(GetAllocationSize(alloc) >= alloc_size);

        list_remove(&alloc.free_siblings);
        alloc.MarkAllocated();
        return &alloc.data;
    }

    void *Allocate(size_t alloc_size) {
        DpoolAllocation *alloc;

        for (alloc = DpoolAllocation::FromFreeHead(free_allocations.next);
             &alloc->free_siblings != &free_allocations;
             alloc = alloc->GetNextFree()) {
            if (GetAllocationSize(*alloc) >= alloc_size)
                return Allocate(*alloc, alloc_size);
        }

        if (sizeof(*alloc) - sizeof(alloc->data) + alloc_size > size - used)
            return nullptr;

        alloc = (DpoolAllocation *)(void *)(data.data + used);
        used += sizeof(*alloc) - sizeof(alloc->data) + alloc_size;

        list_add(&alloc->all_siblings, all_allocations.prev);
        alloc->MarkAllocated();

        return &alloc->data;
    }

    gcc_pure
    size_t GetTotalFreeSize() const {
        size_t result = 0;

        for (auto alloc = DpoolAllocation::FromFreeHead(free_allocations.next);
             &alloc->free_siblings != &free_allocations;
             alloc = DpoolAllocation::FromFreeHead(alloc->free_siblings.next))
            result += GetAllocationSize(*alloc);

        return result;
    }
};

#endif
