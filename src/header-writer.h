/*
 * Write HTTP headers into a fifo_buffer_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HEADER_WRITER_H
#define __BENG_HEADER_WRITER_H

#include "strmap.h"
#include "fifo-buffer.h"

#include <sys/types.h>

struct header_writer {
    fifo_buffer_t buffer;
    strmap_t headers;
    const struct pair *next;
};

void
header_writer_init(struct header_writer *hw, fifo_buffer_t buffer,
                   strmap_t headers);

/**
 * @return -2 if the buffer is full, 0 if there are no more headers,
 * or the number of bytes appended
 */
ssize_t
header_writer_run(struct header_writer *hw);

#endif
