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

    /**
     * The remaining number of bytes.  If that is unknown
     * (i.e. chunked or ended by closing the socket), the value is -1.
     */
    off_t rest;

#ifndef NDEBUG
    bool chunked;
#endif
};

static inline istream_t
http_body_istream(struct http_body_reader *body)
{
    return istream_struct_cast(&body->output);
}

static inline bool
http_body_eof(struct http_body_reader *body)
{
#ifndef NDEBUG
    if (!body->chunked && body->rest == -1)
        /* this is a workaround for the partially incorrect
           assert(!http_body_eof) in
           http_client_response_stream_close(): if the response is
           _not_ chunked, and the handler has been cleared right
           before http_client_response_stream_close() is called, the
           assertion fails, because this function thinks EOF has been
           reached; the debug-only variable body->chunked works around
           that flaw */
        return false;
#endif

    return body->rest == 0 ||
        (/* the dechunker clears our handler when it is finished */
         body->rest == -1 &&
         !istream_has_handler(istream_struct_cast(&body->output)));
}

off_t
http_body_available(const struct http_body_reader *body,
                    const struct fifo_buffer *buffer, bool partial);

/**
 * Returne (size_t)-1 if the buffer is empty.
 */
size_t
http_body_consume_body(struct http_body_reader *body,
                       struct fifo_buffer *buffer);

ssize_t
http_body_try_direct(struct http_body_reader *body, int fd,
                     enum istream_direct fd_type);

/**
 * Determines whether the socket can be released now.  This is true if
 * the body is empty, or if the data in the buffer contains enough for
 * the full response.
 */
bool
http_body_socket_is_done(struct http_body_reader *body,
                         const struct fifo_buffer *buffer);

/**
 * The underlying socket has been closed by the remote.
 *
 * @return true if there is data left in the buffer, false if the body
 * has been finished (with or without error)
 */
bool
http_body_socket_eof(struct http_body_reader *body,
                     struct fifo_buffer *buffer);

istream_t
http_body_init(struct http_body_reader *body,
               const struct istream *stream, pool_t stream_pool,
               pool_t pool, off_t content_length, bool chunked);

#endif
