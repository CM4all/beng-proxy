/*
 * Helper functions for buffered istream implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_BUFFER_HXX
#define BENG_PROXY_ISTREAM_BUFFER_HXX

#include "istream-internal.h"
#include "fifo_buffer.hxx"
#include "util/ConstBuffer.hxx"

/**
 * @return the number of bytes still in the buffer
 */
static inline size_t
istream_buffer_consume(struct istream *istream, struct fifo_buffer *buffer)
{
    auto r = fifo_buffer_read(buffer);
    if (r.IsEmpty())
        return 0;

    size_t consumed = istream_invoke_data(istream, r.data, r.size);
    if (consumed > 0)
        fifo_buffer_consume(buffer, consumed);
    return r.size - consumed;
}

/**
 * @return the number of bytes consumed
 */
static inline size_t
istream_buffer_send(struct istream *istream, struct fifo_buffer *buffer)
{
    auto r = fifo_buffer_read(buffer);
    if (r.IsEmpty())
        return 0;

    size_t consumed = istream_invoke_data(istream, r.data, r.size);
    if (consumed > 0)
        fifo_buffer_consume(buffer, consumed);
    return consumed;
}

#endif
