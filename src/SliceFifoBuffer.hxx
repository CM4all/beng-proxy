/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SLICE_FIFO_BUFFER_HXX
#define BENG_PROXY_SLICE_FIFO_BUFFER_HXX

#include "socket_wrapper.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <stdint.h>

class SliceFifoBuffer : public ForeignFifoBuffer<uint8_t> {
    struct slice_area *area;

public:
    SliceFifoBuffer():ForeignFifoBuffer<uint8_t>(nullptr) {}

    SliceFifoBuffer(struct slice_pool &pool)
        :ForeignFifoBuffer<uint8_t>(nullptr) {
        Allocate(pool);
    }

    void Swap(SliceFifoBuffer &other) {
        ForeignFifoBuffer<uint8_t>::Swap(other);
        std::swap(area, other.area);
    }

    void Allocate(struct slice_pool &pool);
    void Free(struct slice_pool &pool);

    bool IsDefinedAndFull() const {
        return IsDefined() && IsFull();
    }

    void AllocateIfNull(struct slice_pool &pool) {
        if (IsNull())
            Allocate(pool);
    }

    void FreeIfDefined(struct slice_pool &pool) {
        if (IsDefined())
            Free(pool);
    }

    void FreeIfEmpty(struct slice_pool &pool) {
        if (IsEmpty())
            FreeIfDefined(pool);
    }

    /**
     * Move as much data as possible from the specified buffer.  If
     * the destination buffer is empty, the buffers are swapped.  Care
     * is taken that neither buffer suddenly becomes nulled
     * afterwards, because some callers may not be prepared for this.
     */
    void MoveFrom(SliceFifoBuffer &src) {
        if (IsEmpty() && !IsNull() && !src.IsNull())
            /* optimized special case: swap buffer pointers instead of
               copying data */
            Swap(src);
        else
            ForeignFifoBuffer<uint8_t>::MoveFrom(src);
    }

    /**
     * Like MoveFrom(), but allow the destination to be nulled.  This
     * is useful when #src can be freed, but this object cannot.
     */
    void MoveFromAllowNull(SliceFifoBuffer &src) {
        if (IsEmpty() && (!src.IsEmpty() || !IsNull()))
            /* optimized special case: swap buffer pointers instead of
               copying data */
            Swap(src);
        else
            ForeignFifoBuffer<uint8_t>::MoveFrom(src);
    }
};

#endif
