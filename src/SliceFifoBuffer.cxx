/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "SliceFifoBuffer.hxx"
#include "SlicePool.hxx"

void
SliceFifoBuffer::Allocate(SlicePool &pool)
{
    assert(IsNull());

    auto allocation = slice_alloc(&pool);
    area = allocation.area;
    SetBuffer((uint8_t *)allocation.data, allocation.size);
}

void
SliceFifoBuffer::Free(SlicePool &pool)
{
    assert(IsDefined());

    slice_free(&pool, area, GetBuffer());
    SetNull();
}
