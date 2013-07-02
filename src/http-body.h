/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_BODY_H
#define __BENG_HTTP_BODY_H

#include "istream.h"

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct filtered_socket;

struct http_body_reader {
    struct istream output;

    /**
     * The remaining number of bytes.  If that is unknown
     * (i.e. chunked or ended by closing the socket), the value is -1.
     *
     * When the body is chunked, and the EOF chunk has been seen, the
     * value is -2.
     */
    off_t rest;

#ifndef NDEBUG
    bool chunked, socket_eof;
#endif
};

static inline struct istream *
http_body_istream(struct http_body_reader *body)
{
    return istream_struct_cast(&body->output);
}

static inline bool
http_body_eof(struct http_body_reader *body)
{
    return body->rest == 0 || body->rest == -2;
}

gcc_pure
off_t
http_body_available(const struct http_body_reader *body,
                    const struct filtered_socket *s, bool partial);

size_t
http_body_feed_body(struct http_body_reader *body,
                    const void *data, size_t length);

ssize_t
http_body_try_direct(struct http_body_reader *body, int fd,
                     enum istream_direct fd_type);

/**
 * Determines whether the socket can be released now.  This is true if
 * the body is empty, or if the data in the buffer contains enough for
 * the full response.
 */
gcc_pure
bool
http_body_socket_is_done(struct http_body_reader *body,
                         const struct filtered_socket *s);

/**
 * The underlying socket has been closed by the remote.
 *
 * @return true if there is data left in the buffer, false if the body
 * has been finished (with or without error)
 */
bool
http_body_socket_eof(struct http_body_reader *body, size_t remaining);

struct istream *
http_body_init(struct http_body_reader *body,
               const struct istream_class *stream, struct pool *stream_pool,
               struct pool *pool, off_t content_length, bool chunked);

#endif
