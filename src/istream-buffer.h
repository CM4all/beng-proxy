/*
 * Helper functions for buffered istream implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __ISTREAM_BUFFER_H
#define __ISTREAM_BUFFER_H

#include "istream.h"
#include "fifo-buffer.h"

/**
 * @return the number of bytes still in the buffer
 */
static inline size_t
istream_buffer_consume(struct istream *istream, fifo_buffer_t buffer)
{
    const void *data;
    size_t length, consumed;
    
    data = fifo_buffer_read(buffer, &length);
    if (data == NULL)
        return 0;

    consumed = istream_invoke_data(istream, data, length);
    if (consumed > 0)
        fifo_buffer_consume(buffer, consumed);
    return length - consumed;
}

/**
 * @return the number of bytes consumed
 */
static inline size_t
istream_buffer_send(struct istream *istream, fifo_buffer_t buffer)
{
    const void *data;
    size_t length, consumed;
    
    data = fifo_buffer_read(buffer, &length);
    if (data == NULL)
        return 0;

    consumed = istream_invoke_data(istream, data, length);
    if (consumed > 0)
        fifo_buffer_consume(buffer, consumed);
    return consumed;
}

#endif
