/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SLICE_FIFO_BUFFER_HXX
#define BENG_PROXY_SLICE_FIFO_BUFFER_HXX

#include "util/ForeignFifoBuffer.hxx"

#include <stdint.h>

struct SlicePool;
struct SliceArea;

class SliceFifoBuffer : public ForeignFifoBuffer<uint8_t> {
    SliceArea *area;

public:
    SliceFifoBuffer():ForeignFifoBuffer<uint8_t>(nullptr) {}

    SliceFifoBuffer(SlicePool &pool)
        :ForeignFifoBuffer<uint8_t>(nullptr) {
        Allocate(pool);
    }

    void Swap(SliceFifoBuffer &other) {
        ForeignFifoBuffer<uint8_t>::Swap(other);
        std::swap(area, other.area);
    }

    void Allocate(SlicePool &pool);
    void Free(SlicePool &pool);

    bool IsDefinedAndFull() const {
        return IsDefined() && IsFull();
    }

    void AllocateIfNull(SlicePool &pool) {
        if (IsNull())
            Allocate(pool);
    }

    void FreeIfDefined(SlicePool &pool) {
        if (IsDefined())
            Free(pool);
    }

    void FreeIfEmpty(SlicePool &pool) {
        if (IsEmpty())
            FreeIfDefined(pool);
    }

    /**
     * If this buffer is empty, free the buffer and reallocate a new
     * one.  This is useful to work around #SliceArea fragmentation.
     */
    void CycleIfEmpty(SlicePool &pool) {
        if (IsDefined() && IsEmpty()) {
            Free(pool);
            Allocate(pool);
        }
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

    /**
     * Like MoveFrom(), but allow the source to be nulled.  This is
     * useful when this object can be freed, but #src cannot.
     */
    void MoveFromAllowSrcNull(SliceFifoBuffer &src) {
        if (IsEmpty() && (!src.IsEmpty() || IsNull()))
            /* optimized special case: swap buffer pointers instead of
               copying data */
            Swap(src);
        else
            ForeignFifoBuffer<uint8_t>::MoveFrom(src);
    }

    /**
     * Swaps the two buffers if #src is nulled.  This is useful when
     * #src can be freed, but this object cannot.
     */
    void SwapIfNull(SliceFifoBuffer &src) {
        if (src.IsNull() && IsEmpty() && !IsNull())
            Swap(src);
    }
};

#endif
