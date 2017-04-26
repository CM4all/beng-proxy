/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "dpool.hxx"
#include "dchunk.hxx"
#include "shm.hxx"

#include <inline/compiler.h>
#include <inline/poison.h>

#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include <assert.h>

struct dpool {
    struct shm *const shm;
    mutable boost::interprocess::interprocess_mutex mutex;

    /**
     * Counts the number of d_free() calls.  After a certain number of
     * calls, we assume that the pool is "fragmented" and the session
     * shall be duplicated to a new pool.
     */
    unsigned free_counter = 0;

    DpoolChunk first_chunk;

    explicit dpool(struct shm &_shm);
    ~dpool();

    void *Allocate(size_t size) throw(std::bad_alloc);
    void Free(const void *p);

private:
    gcc_pure
    DpoolChunk *FindChunk(const void *p);
};

dpool::dpool(struct shm &_shm)
    :shm(&_shm),
     first_chunk(shm_page_size(shm) - sizeof(*this) +
                 sizeof(first_chunk))
{
    assert(shm_page_size(shm) >= sizeof(*this));

    list_init(&first_chunk.siblings);
}

dpool::~dpool()
{
    assert(shm != nullptr);

    DpoolChunk *chunk, *n;
    for (chunk = (DpoolChunk *)(void *)first_chunk.siblings.next;
         chunk != &first_chunk; chunk = n) {
        n = (DpoolChunk *)(void *)chunk->siblings.next;

        chunk->Destroy(*shm);
    }
}

struct dpool *
dpool_new(struct shm &shm)
{
    return NewFromShm<struct dpool>(&shm, 1, shm);
}

void
dpool_destroy(struct dpool *pool)
{
    assert(pool != nullptr);

    DeleteFromShm(pool->shm, pool);
}

bool
dpool_is_fragmented(const struct dpool &pool)
{
    return pool.free_counter >= 256;
}

inline void *
dpool::Allocate(size_t size) throw(std::bad_alloc)
{
    assert(shm != nullptr);

    /* we could theoretically allow larger allocations by using
       multiple consecutive chunks, but we don't implement that
       because our current use cases should not need to allocate such
       large structures */
    if (size > first_chunk.GetTotalSize())
        throw std::bad_alloc();

    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> scoped_lock(mutex);

    /* find a chunk with enough room */

    auto *chunk = &first_chunk;
    do {
        void *p = chunk->Allocate(size);
        if (p != nullptr)
            return p;

        chunk = (DpoolChunk *)(void *)chunk->siblings.next;
    } while (chunk != &first_chunk);

    /* none found; try to allocate a new chunk */

    chunk = DpoolChunk::New(*shm);
    if (chunk == nullptr)
        throw std::bad_alloc();

    list_add(&chunk->siblings, &first_chunk.siblings);

    void *p = chunk->Allocate(size);
    assert(p != nullptr);
    return p;
}

void *
d_malloc(struct dpool &pool, size_t size)
    throw(std::bad_alloc)
{
    return pool.Allocate(size);
}

inline DpoolChunk *
dpool::FindChunk(const void *p)
{
    if (first_chunk.Contains(p))
        return &first_chunk;

    for (auto *chunk = (DpoolChunk *)(void *)first_chunk.siblings.next;
         chunk != &first_chunk;
         chunk = (DpoolChunk *)(void *)chunk->siblings.next) {
        if (chunk->Contains(p))
            return chunk;
    }

    return nullptr;
}

inline void
dpool::Free(const void *p)
{
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> scoped_lock(mutex);

    ++free_counter;

    auto *chunk = FindChunk(p);
    assert(chunk != nullptr);

    chunk->Free(const_cast<void *>(p));

    if (chunk->IsEmpty() && chunk != &first_chunk) {
        /* the chunk is completely empty; release it to the SHM
           object */
        list_remove(&chunk->siblings);
        chunk->Destroy(*shm);
    }
}

void
d_free(struct dpool &pool, const void *p)
{
    pool.Free(p);
}
