/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-body.h"
#include "istream-internal.h"

#include <assert.h>
#include <limits.h>

/** determine how much can be read from the body */
static inline size_t
http_body_max_read(struct http_body_reader *body, size_t length)
{
    if (body->rest != (off_t)-1 && body->rest < (off_t)length)
        return (size_t)body->rest;
    else
        return length;
}

static void
http_body_consumed(struct http_body_reader *body, size_t nbytes)
{
    pool_t pool = body->output.pool;

    if (body->rest == (off_t)-1) {
        if (body->dechunk != NULL && istream_dechunk_eof(body->dechunk))
            body->rest = 0;

        return;
    }

    assert((off_t)nbytes <= body->rest);

    body->rest -= (off_t)nbytes;
    if (body->rest > 0)
        return;

    pool_ref(pool);

    istream_invoke_eof(&body->output);

    pool_unref(pool);
}

void
http_body_consume_body(struct http_body_reader *body,
                       fifo_buffer_t buffer)
{
    const void *data;
    size_t length, consumed;

    data = fifo_buffer_read(buffer, &length);
    if (data == NULL)
        return;

    length = http_body_max_read(body, length);
    consumed = istream_invoke_data(&body->output, data, length);
    if (consumed == 0)
        return;

    fifo_buffer_consume(buffer, consumed);
    http_body_consumed(body, consumed);
}

ssize_t
http_body_try_direct(struct http_body_reader *body, int fd)
{
    ssize_t nbytes;

    assert(fd >= 0);
    assert(body->output.handler_direct & ISTREAM_SOCKET);
    assert(body->output.handler->direct != NULL);

    nbytes = istream_invoke_direct(&body->output,
                                   ISTREAM_SOCKET, fd,
                                   http_body_max_read(body, INT_MAX));
    if (nbytes > 0)
        http_body_consumed(body, (size_t)nbytes);

    return nbytes;
}

void
http_body_socket_eof(struct http_body_reader *body, fifo_buffer_t buffer)
{
    (void)buffer; /* XXX there may still be data in here */

    if (body->rest > 0)
        istream_invoke_abort(&body->output);
    else
        istream_invoke_eof(&body->output);
}

istream_t
http_body_init(struct http_body_reader *body,
               const struct istream *stream, pool_t stream_pool,
               pool_t pool, off_t content_length, int keep_alive)
{
    istream_t istream;

    assert(pool_contains(stream_pool, body, sizeof(*body)));

    body->output = *stream;
    body->output.pool = stream_pool;
    body->rest = content_length;

    istream = http_body_istream(body);
    if (keep_alive && content_length == (off_t)-1)
        istream = body->dechunk = istream_dechunk_new(pool, istream);
    else
        body->dechunk = NULL;

    return istream;
}
