/*
 * Utilities for buffered I/O.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_BUFFERED_IO_H
#define __BENG_BUFFERED_IO_H

#include "fifo-buffer.h"

#include <sys/types.h>

ssize_t
buffered_quick_write(int fd, fifo_buffer_t output_buffer,
                     const void *buffer, size_t length);

#endif
