/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "DefaultChunkAllocator.hxx"
#include "SlicePool.hxx"
#include "fb_pool.hxx"
#include "util/WritableBuffer.hxx"

#include <assert.h>

#ifndef NDEBUG

DefaultChunkAllocator::~DefaultChunkAllocator()
{
    assert(area == nullptr);
}

#endif

WritableBuffer<void>
DefaultChunkAllocator::Allocate()
{
    assert(area == nullptr);

    auto a = slice_alloc(&fb_pool_get());
    area = a.area;
    return {a.data, a.size};
}

void
DefaultChunkAllocator::Free(void *p)
{
    assert(area != nullptr);

    slice_free(&fb_pool_get(), area, p);

    area = nullptr;
}
