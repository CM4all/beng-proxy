/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_BODY_H
#define __BENG_HTTP_BODY_H

#include "istream.h"

#include <stddef.h>

struct fifo_buffer;

struct http_body_reader {
    struct istream output;
    off_t rest;
};

static inline istream_t
http_body_istream(struct http_body_reader *body)
{
    return istream_struct_cast(&body->output);
}

static inline bool
http_body_eof(struct http_body_reader *body)
{
    return body->rest == 0 ||
        (/* the dechunker clears our handler when it is finished */
         body->rest == -1 &&
         !istream_has_handler(istream_struct_cast(&body->output)));
}

static inline off_t
http_body_available(const struct http_body_reader *body)
{
    return body->rest;
}

size_t
http_body_consume_body(struct http_body_reader *body,
                       struct fifo_buffer *buffer);

ssize_t
http_body_try_direct(struct http_body_reader *body, int fd);

/**
 * The underlying socket has been closed by the remote.  Handle the
 * rest from the input buffer and forward eof/abort to the istream
 * handler.
 */
void
http_body_socket_eof(struct http_body_reader *body,
                     struct fifo_buffer *buffer);

istream_t
http_body_init(struct http_body_reader *body,
               const struct istream *stream, pool_t stream_pool,
               pool_t pool, off_t content_length, bool keep_alive);

#endif
