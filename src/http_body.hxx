/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_BODY_HXX
#define BENG_PROXY_HTTP_BODY_HXX

#include "istream.h"

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct filtered_socket;

struct http_body_reader {
    struct istream output;

    /**
     * The remaining number of bytes.
     *
     * @see #HTTP_BODY_REST_UNKNOWN, #HTTP_BODY_REST_EOF_CHUNK,
     * #HTTP_BODY_REST_CHUNKED
     */
    off_t rest;

#ifndef NDEBUG
    bool chunked, socket_eof;
#endif
};

/**
 * The remaining size is unknown.
 */
static constexpr off_t HTTP_BODY_REST_UNKNOWN = -1;

/**
 * EOF chunk has been seen.
 */
static constexpr off_t HTTP_BODY_REST_EOF_CHUNK = -2;

/**
 * Chunked response.  Will flip to #HTTP_BODY_REST_EOF_CHUNK as soon
 * as the EOF chunk is seen.
 */
static constexpr off_t HTTP_BODY_REST_CHUNKED = -3;

static inline struct istream *
http_body_istream(struct http_body_reader *body)
{
    return istream_struct_cast(&body->output);
}

static inline bool
http_body_chunked(const struct http_body_reader *body)
{
    return body->rest == HTTP_BODY_REST_CHUNKED;
}

static inline bool
http_body_eof(const struct http_body_reader *body)
{
    return body->rest == 0 || body->rest == HTTP_BODY_REST_EOF_CHUNK;
}

/**
 * Do we require more data to finish the body?
 */
static inline bool
http_body_require_more(const struct http_body_reader *body)
{
    return body->rest > 0 || body->rest == HTTP_BODY_REST_CHUNKED;
}

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
