/*
 * Utilities for buffered I/O.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_BUFFERED_IO_HXX
#define BENG_PROXY_BUFFERED_IO_HXX

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
template<typename B>
ssize_t
read_to_buffer(int fd, B &buffer, size_t length);

/**
 * Writes data from the buffer to the file.
 *
 * @param fd the destination file descriptor
 * @param buffer the source buffer
 * @return -1 on error, -2 if the buffer is empty, or the rest left in the buffer
 */
template<typename B>
ssize_t
write_from_buffer(int fd, B &buffer);

/**
 * Appends data from a socket to the buffer.
 *
 * @param fd the source socket
 * @param buffer the destination buffer
 * @return -1 on error, -2 if the buffer is full, or the amount appended to the buffer
 */
template<typename B>
ssize_t
recv_to_buffer(int fd, B &buffer, size_t length);

/**
 * Sends data from the buffer to the socket.
 *
 * @param fd the destination socket
 * @param buffer the source buffer
 * @return -1 on error, -2 if the buffer is empty, or the rest left in the buffer
 */
template<typename B>
ssize_t
send_from_buffer(int fd, B &buffer);

#endif
