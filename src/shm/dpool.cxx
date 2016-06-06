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
};

dpool::dpool(struct shm &_shm)
    :shm(&_shm),
     first_chunk(shm_page_size(shm) - sizeof(*this) +
                 sizeof(first_chunk.data))
{
    assert(shm_page_size(shm) >= sizeof(*this));

    list_init(&first_chunk.siblings);
}

dpool::~dpool()
{
    assert(shm != nullptr);

    DpoolChunk *chunk, *n;
    for (chunk = (DpoolChunk *)first_chunk.siblings.next;
         chunk != &first_chunk; chunk = n) {
        n = (DpoolChunk *)chunk->siblings.next;

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

void *
d_malloc(struct dpool *pool, size_t size)
    throw(std::bad_alloc)
{
    void *p;

    assert(pool != nullptr);
    assert(pool->shm != nullptr);

    /* we could theoretically allow larger allocations by using
       multiple consecutive chunks, but we don't implement that
       because our current use cases should not need to allocate such
       large structures */
    if (size > pool->first_chunk.GetTotalSize())
        throw std::bad_alloc();

    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> scoped_lock(pool->mutex);

    /* find a chunk with enough room */

    auto *chunk = &pool->first_chunk;
    do {
        p = chunk->Allocate(size);
        if (p != nullptr)
            return p;

        chunk = (DpoolChunk *)chunk->siblings.next;
    } while (chunk != &pool->first_chunk);

    /* none found; try to allocate a new chunk */

    assert(p == nullptr);

    chunk = DpoolChunk::New(*pool->shm, pool->first_chunk.siblings);
    if (chunk == nullptr)
        throw std::bad_alloc();

    p = chunk->Allocate(size);
    assert(p != nullptr);
    return p;
}

static DpoolChunk *
dpool_find_chunk(struct dpool &pool, const void *p)
{
    if (pool.first_chunk.Contains(p))
        return &pool.first_chunk;

    for (auto *chunk = (DpoolChunk *)pool.first_chunk.siblings.next;
         chunk != &pool.first_chunk;
         chunk = (DpoolChunk *)chunk->siblings.next) {
        if (chunk->Contains(p))
            return chunk;
    }

    return nullptr;
}

void
d_free(struct dpool *pool, const void *p)
{
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> scoped_lock(pool->mutex);

    ++pool->free_counter;

    auto *chunk = dpool_find_chunk(*pool, p);
    assert(chunk != nullptr);

    chunk->Free(const_cast<void *>(p));

    if (chunk->IsEmpty() && chunk != &pool->first_chunk) {
        /* the chunk is completely empty; release it to the SHM
           object */
        list_remove(&chunk->siblings);
        chunk->Destroy(*pool->shm);
    }
}
