/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-body.h"
#include "istream-internal.h"
#include "fifo-buffer.h"

#include <assert.h>
#include <limits.h>

off_t
http_body_available(const struct http_body_reader *body,
                    const struct fifo_buffer *buffer, bool partial)
{
    if (body->rest >= 0)
        return body->rest;

    return partial
        ? (off_t)fifo_buffer_available(buffer)
        : -1;
}

/** determine how much can be read from the body */
static inline size_t
http_body_max_read(struct http_body_reader *body, size_t length)
{
    if (body->rest != (off_t)-1 && body->rest < (off_t)length)
        /* content-length header was provided, return this value */
        return (size_t)body->rest;
    else
        /* read as much as possible, the dechunker will do the rest */
        return length;
}

static void
http_body_consumed(struct http_body_reader *body, size_t nbytes)
{
    if (body->rest == (off_t)-1)
        return;

    assert((off_t)nbytes <= body->rest);

    body->rest -= (off_t)nbytes;
}

size_t
http_body_consume_body(struct http_body_reader *body,
                       struct fifo_buffer *buffer)
{
    const void *data;
    size_t length, consumed;

    data = fifo_buffer_read(buffer, &length);
    if (data == NULL)
        return (size_t)-1;

    length = http_body_max_read(body, length);
    consumed = istream_invoke_data(&body->output, data, length);
    if (consumed > 0) {
        fifo_buffer_consume(buffer, consumed);
        http_body_consumed(body, consumed);
    }

    return consumed;
}

ssize_t
http_body_try_direct(struct http_body_reader *body, int fd,
                     enum istream_direct fd_type)
{
    ssize_t nbytes;

    assert(fd >= 0);
    assert(body->output.handler_direct & fd_type);
    assert(body->output.handler->direct != NULL);

    nbytes = istream_invoke_direct(&body->output,
                                   fd_type, fd,
                                   http_body_max_read(body, INT_MAX));
    if (nbytes > 0)
        http_body_consumed(body, (size_t)nbytes);

    return nbytes;
}

bool
http_body_socket_is_done(struct http_body_reader *body,
                         const struct fifo_buffer *buffer)
{
    return body->rest != -1 &&
        (body->rest == 0 ||
         (off_t)fifo_buffer_available(buffer) >= body->rest);
}

bool
http_body_socket_eof(struct http_body_reader *body, struct fifo_buffer *buffer)
{
    const void *data;
    size_t length;

    /* see how much is left in the buffer */
    data = fifo_buffer_read(buffer, &length);
    if (data == NULL)
        length = 0;

    if (body->rest == -1) {
        if (length > 0) {
            /* serve the rest of the buffer, then end the body
               stream */
            body->rest = length;
            return true;
        }

        /* the socket is closed, which ends the body */
        istream_deinit_eof(&body->output);
        return false;
    } else if (body->rest == (off_t)length) {
        if (length > 0)
            /* serve the rest of the buffer, then end the body
               stream */
            return true;

        istream_deinit_eof(&body->output);
        return false;
    } else {
        /* something has gone wrong: either not enough or too much
           data left in the buffer */
        istream_deinit_abort(&body->output);
        return false;
    }
}

istream_t
http_body_init(struct http_body_reader *body,
               const struct istream *stream, pool_t stream_pool,
               pool_t pool, off_t content_length, bool chunked)
{
    istream_t istream;

    assert(pool_contains(stream_pool, body, sizeof(*body)));

    istream_init(&body->output, stream, stream_pool);
    body->rest = content_length;

#ifndef NDEBUG
    body->chunked = chunked;
#endif

    istream = http_body_istream(body);
    if (chunked) {
        assert(content_length == (off_t)-1);

        istream = istream_dechunk_new(pool, istream);
    }

    return istream;
}
