/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dpool.hxx"
#include "dchunk.hxx"
#include "shm.hxx"

#include "util/Compiler.h"

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

    DpoolChunk::List chunks;

    DpoolChunk first_chunk;

    explicit dpool(struct shm &_shm);
    ~dpool();

    void *Allocate(size_t size);
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

    chunks.push_front(first_chunk);
}

dpool::~dpool()
{
    assert(shm != nullptr);

    assert(&chunks.back() == &first_chunk);
    chunks.pop_back();

    chunks.clear_and_dispose(DpoolChunk::Disposer(*shm));
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
dpool::Allocate(size_t size)
{
    assert(shm != nullptr);

    /* we could theoretically allow larger allocations by using
       multiple consecutive chunks, but we don't implement that
       because our current use cases should not need to allocate such
       large structures */
    if (size + first_chunk.GetMinSize() > first_chunk.GetTotalSize())
        throw std::bad_alloc();

    boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> scoped_lock(mutex);

    /* find a chunk with enough room */

    for (auto &chunk : chunks) {
        void *p = chunk.Allocate(size);
        if (p != nullptr)
            return p;
    }

    /* none found; try to allocate a new chunk */

    auto *chunk = DpoolChunk::New(*shm);
    if (chunk == nullptr)
        throw std::bad_alloc();

    chunks.push_front(*chunk);

    void *p = chunk->Allocate(size);
    assert(p != nullptr);
    return p;
}

void *
d_malloc(struct dpool &pool, size_t size)
{
    return pool.Allocate(size);
}

inline DpoolChunk *
dpool::FindChunk(const void *p)
{
    for (auto &chunk : chunks)
        if (chunk.Contains(p))
            return &chunk;

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
        chunks.erase_and_dispose(chunks.iterator_to(*chunk),
                                 DpoolChunk::Disposer(*shm));
    }
}

void
d_free(struct dpool &pool, const void *p)
{
    pool.Free(p);
}
