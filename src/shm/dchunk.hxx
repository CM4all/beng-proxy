/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_DCHUNK_HXX
#define SHM_DCHUNK_HXX

#include "shm.hxx"

#include <inline/compiler.h>
#include <inline/list.h>

#include <boost/interprocess/managed_external_buffer.hpp>

#include <assert.h>
#include <stddef.h>

struct DpoolChunk {
    struct list_head siblings;

    boost::interprocess::managed_external_buffer m;

    struct alignas(16) {
        size_t data[1];
    } data;

    /**
     * @param total_size the size of the memory allocation this
     * #DpoolChunk lives in; it is used to calculate the usable chunk
     * size
     */
    explicit DpoolChunk(size_t total_size)
        :m(boost::interprocess::create_only, &data,
           total_size - sizeof(*this) + sizeof(data)) {
        assert(total_size >= sizeof(*this));
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

    bool IsEmpty() const {
        return const_cast<DpoolChunk *>(this)->m.all_memory_deallocated();
    }

    gcc_pure
    bool Contains(const void *p) const {
        return (const unsigned char *)p >= (const unsigned char *)&data &&
            (const unsigned char *)p < (const unsigned char *)&data + m.get_size();
    }

    void *Allocate(size_t alloc_size) {
        return m.allocate_aligned(alloc_size, 16, std::nothrow);
    }

    void Free(void *alloc) {
        m.deallocate(alloc);
    }

    gcc_pure
    size_t GetTotalSize() const {
        return m.get_size();
    }
};

#endif
