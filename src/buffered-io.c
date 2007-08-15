/*
 * Utilities for buffered I/O.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "buffered-io.h"

#include <unistd.h>
#include <errno.h>

ssize_t
buffered_quick_write(int fd, fifo_buffer_t output_buffer,
                     const void *data, size_t length) {
    if (fifo_buffer_empty(output_buffer)) {
        /* to save time, we handle a special but very common case
           here: the output buffer is empty, and we're going to add
           data now.  since the socket is non-blocking, we immediately
           try to commit the new data to the socket */
        ssize_t nbytes;

        nbytes = write(fd, data, length);
        if (nbytes <= 0) {
            /* didn't work - postpone the new data block */
            if (nbytes < 0 && errno == EAGAIN)
                nbytes = 0;
            fifo_buffer_append(output_buffer, length);
        } else if (nbytes > 0 && (size_t)nbytes < length) {
            /* some was sent */
            fifo_buffer_append(output_buffer, length);
            fifo_buffer_consume(output_buffer, (size_t)nbytes);
        } else {
            /* everything was sent - do it again! */
        }

        return nbytes;
    } else {
        /* don't quick-write */
        fifo_buffer_append(output_buffer, length);
        return 0;
    }
}
