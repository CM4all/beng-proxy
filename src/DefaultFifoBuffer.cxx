/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "DefaultFifoBuffer.hxx"
#include "fb_pool.hxx"

void
DefaultFifoBuffer::Allocate()
{
    SliceFifoBuffer::Allocate(fb_pool_get());
}

void
DefaultFifoBuffer::Free()
{
    SliceFifoBuffer::Free(fb_pool_get());
}

void
DefaultFifoBuffer::AllocateIfNull()
{
    SliceFifoBuffer::AllocateIfNull(fb_pool_get());
}

void
DefaultFifoBuffer::FreeIfDefined()
{
    SliceFifoBuffer::FreeIfDefined(fb_pool_get());
}

void
DefaultFifoBuffer::FreeIfEmpty()
{
    SliceFifoBuffer::FreeIfEmpty(fb_pool_get());
}

void
DefaultFifoBuffer::CycleIfEmpty()
{
    SliceFifoBuffer::CycleIfEmpty(fb_pool_get());
}
