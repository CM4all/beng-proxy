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

#if defined(__x86_64__) || defined(__PPC64__)
#define ALIGN 8
#define ALIGN_BITS 0x7
#else
#define ALIGN 4
#define ALIGN_BITS 0x3
#endif

struct dpool {
    struct shm *const shm;
    mutable boost::interprocess::interprocess_mutex mutex;
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
    assert(first_chunk.size == shm_page_size(shm) - sizeof(*this) +
           sizeof(first_chunk.data));

    DpoolChunk *chunk, *n;
    for (chunk = (DpoolChunk *)first_chunk.siblings.next;
         chunk != &first_chunk; chunk = n) {
        n = (DpoolChunk *)chunk->siblings.next;

        chunk->Destroy(*shm);
    }
}

static constexpr size_t
align_size(size_t size)
{
    return ((size - 1) | ALIGN_BITS) + 1;
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
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> scoped_lock(pool.mutex);

    size_t reserved = 0, freed = 0;
    const DpoolChunk *chunk = &pool.first_chunk;

    do {
        reserved += chunk->used;
        freed += chunk->GetTotalFreeSize();

        chunk = (DpoolChunk *)chunk->siblings.next;
    } while (chunk != &pool.first_chunk);

    return reserved > 0 && freed * 4 > reserved;
}

void *
d_malloc(struct dpool *pool, size_t size)
    throw(std::bad_alloc)
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

    auto *chunk = dpool_find_chunk(*pool, p);
    auto *alloc = &DpoolAllocation::FromPointer(p);

    assert(chunk != nullptr);
    assert(alloc->IsAllocated());

    chunk->Free(alloc);

    if (chunk->IsEmpty() && chunk != &pool->first_chunk) {
        /* the chunk is completely empty; release it to the SHM
           object */
        list_remove(&chunk->siblings);
        chunk->Destroy(*pool->shm);
    }
}
