/*
 * Utilities for buffered I/O (struct growing_buffer).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GB_IO_H
#define __BENG_GB_IO_H

#include <sys/types.h>

struct growing_buffer;

/**
 * Writes data from the buffer to the file.
 *
 * @param fd the destination file descriptor
 * @param buffer the source buffer
 * @return -1 on error, -2 if the buffer is empty, or the rest left in the buffer
 */
ssize_t
write_from_gb(int fd, struct growing_buffer *gb);

/**
 * Sends data from the buffer to the socket.
 *
 * @param fd the destination socket
 * @param buffer the source buffer
 * @return -1 on error, -2 if the buffer is empty, or the rest left in the buffer
 */
ssize_t
send_from_gb(int fd, struct growing_buffer *gb);

#endif
