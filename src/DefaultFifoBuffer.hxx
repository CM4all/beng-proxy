/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DEFAULT_FIFO_BUFFER_HXX
#define BENG_PROXY_DEFAULT_FIFO_BUFFER_HXX

#include "SliceFifoBuffer.hxx"

/**
 * A frontend for #SliceFifoBuffer which allows to replace it with a
 * simple heap-allocated buffer when some client code gets copied to
 * another project.
 */
class DefaultFifoBuffer : public SliceFifoBuffer {
public:
    void Allocate();
    void Free();
    void AllocateIfNull();
    void FreeIfDefined();
    void FreeIfEmpty();
    void CycleIfEmpty();
};

#endif
