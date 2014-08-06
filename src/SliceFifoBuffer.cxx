/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "SliceFifoBuffer.hxx"
#include "SlicePool.hxx"

void
SliceFifoBuffer::Allocate(struct slice_pool &pool)
{
    assert(IsNull());

    area = slice_pool_get_area(&pool);
    assert(area != nullptr);

    SetBuffer((uint8_t *)slice_alloc(&pool, area),
              slice_pool_get_slice_size(&pool));
}

void
SliceFifoBuffer::Free(struct slice_pool &pool)
{
    assert(IsDefined());

    slice_free(&pool, area, GetBuffer());
    SetNull();
}
