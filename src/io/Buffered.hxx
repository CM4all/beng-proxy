/*
 * Utilities for buffered I/O.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef IO_BUFFERED_HXX
#define IO_BUFFERED_HXX

#include <sys/types.h>
#include <stdint.h>

template<typename T> class ForeignFifoBuffer;

/**
 * Appends data from a file to the buffer.
 *
 * @param fd the source file descriptor
 * @param buffer the destination buffer
 * @return -1 on error, -2 if the buffer is full, or the amount appended to the buffer
 */
ssize_t
read_to_buffer(int fd, ForeignFifoBuffer<uint8_t> &buffer, size_t length);

/**
 * Writes data from the buffer to the file.
 *
 * @param fd the destination file descriptor
 * @param buffer the source buffer
 * @return -1 on error, -2 if the buffer is empty, or the number of
 * bytes written
 */
ssize_t
write_from_buffer(int fd, ForeignFifoBuffer<uint8_t> &buffer);

#endif
