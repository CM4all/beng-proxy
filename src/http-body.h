/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_BODY_H
#define __BENG_HTTP_BODY_H

#include "istream.h"
#include "fifo-buffer.h"
#include "valgrind.h"

#include <assert.h>
#include <stddef.h>

struct http_body_reader {
    struct istream output;
    off_t rest;
    int dechunk_eof;
};

static inline istream_t
http_body_istream(struct http_body_reader *body)
{
    return istream_struct_cast(&body->output);
}

static inline int
http_body_eof(const struct http_body_reader *body)
{
    return body->rest == 0 ||
        (body->rest == (off_t)-1 && body->dechunk_eof);
}

static inline off_t
http_body_available(const struct http_body_reader *body)
{
    return body->rest;
}

void
http_body_consume_body(struct http_body_reader *body,
                       fifo_buffer_t buffer);

ssize_t
http_body_try_direct(struct http_body_reader *body, int fd);

/**
 * Callback for istream_dechunk_new().
 */
void
http_body_dechunked_eof(void *ctx);

static inline void
http_body_init(struct http_body_reader *body,
               const struct istream *stream, pool_t pool,
               off_t content_length)
{
    assert(pool_contains(pool, body, sizeof(*body)));

    body->output = *stream;
    body->output.pool = pool;
    body->rest = content_length;
    body->dechunk_eof = 0;
}

static inline void
http_body_deinit(struct http_body_reader *body)
{
    VALGRIND_MAKE_MEM_UNDEFINED(body, sizeof(*body));

    body->output.pool = NULL;
}

#endif
