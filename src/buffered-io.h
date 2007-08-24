/*
 * Utilities for buffered I/O.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_BUFFERED_IO_H
#define __BENG_BUFFERED_IO_H

#include "fifo-buffer.h"

#include <sys/types.h>

/**
 * Appends data from a file to the buffer.
 *
 * @param fd the source file descriptor
 * @param buffer the destination buffer
 * @return -1 on error, -2 if the buffer is full, or the amount appended to the buffer
 */
ssize_t
read_to_buffer(int fd, fifo_buffer_t buffer, size_t length);

/**
 * Writes data from the buffer to the file.
 *
 * @param fd the destination file descriptor
 * @param buffer the source buffer
 * @return -1 on error, -2 if the buffer is empty, or the rest left in the buffer
 */
ssize_t
write_from_buffer(int fd, fifo_buffer_t buffer);

ssize_t
buffered_quick_write(int fd, fifo_buffer_t output_buffer,
                     const void *data, size_t length);

#endif
