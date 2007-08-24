/*
 * Write HTTP headers into a fifo_buffer_t.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HEADER_WRITER_H
#define __BENG_HEADER_WRITER_H

#include "growing-buffer.h"

void
header_write(growing_buffer_t gb, const char *key, const char *value);

#endif
