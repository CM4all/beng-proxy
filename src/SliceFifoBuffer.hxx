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

    void Allocate(struct slice_pool &pool);
    void Free(struct slice_pool &pool);
};

#endif
