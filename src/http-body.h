/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_BODY_H
#define __BENG_HTTP_BODY_H

#include "istream.h"
#include "fifo-buffer.h"

#include <stddef.h>

struct http_body_reader {
    struct istream output;
    off_t rest;
};

void
http_body_consume_body(struct http_body_reader *body,
                       fifo_buffer_t buffer);

ssize_t
http_body_try_direct(struct http_body_reader *body, int fd);

#endif
