/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_DCHUNK_HXX
#define SHM_DCHUNK_HXX

#include "shm.hxx"

#include "util/Compiler.h"

#include <boost/interprocess/managed_external_buffer.hpp>
#include <boost/intrusive/list.hpp>

#include <assert.h>
#include <stddef.h>

struct DpoolChunk {
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;

    SiblingsHook siblings;

    boost::interprocess::managed_external_buffer m;

    struct alignas(16) {
        size_t data[1];
    } data;

    typedef boost::intrusive::list<DpoolChunk,
                                   boost::intrusive::member_hook<DpoolChunk,
                                                                 DpoolChunk::SiblingsHook,
                                                                 &DpoolChunk::siblings>,
                                   boost::intrusive::constant_time_size<false>> List;

    explicit DpoolChunk(size_t total_size)
        :m(boost::interprocess::create_only, &data,
           total_size - sizeof(*this) + sizeof(data)) {
        assert(total_size >= sizeof(*this));
    }

    static DpoolChunk *New(struct shm &shm) {
        assert(shm_page_size(&shm) >= sizeof(DpoolChunk));

        return NewFromShm<DpoolChunk>(&shm, 1, shm_page_size(&shm));
    }

    void Destroy(struct shm &shm) {
        DeleteFromShm(&shm, this);
    }

    class Disposer {
        struct shm &shm;

    public:
        explicit Disposer(struct shm &_shm):shm(_shm) {}

        void operator()(DpoolChunk *chunk) {
            chunk->Destroy(shm);
        }
    };

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
