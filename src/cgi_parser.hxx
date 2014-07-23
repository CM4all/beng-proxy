/*
 * Parsing CGI responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_PARSER_HXX
#define BENG_PROXY_CGI_PARSER_HXX

#include "completion.h"
#include "glibfwd.hxx"

#include <http/status.h>

#include <assert.h>
#include <stddef.h>
#include <sys/types.h>

struct pool;
struct fifo_buffer;

/**
 * A parser for the CGI response.
 *
 * - initialize with cgi_parser_init()
 *
 * - pass data received from the CGI program to
 * cgi_parser_feed_headers(), repeat with more data until it returns
 * C_ERROR or C_DONE
 *
 * - after C_DONE, call cgi_parser_get_headers()
 *
 * - use cgi_parser_available() and cgi_parser_consumed() while
 * transferring the response body
 */
struct CGIParser {
    http_status_t status;

    /**
     * The remaining number of bytes in the response body, -1 if
     * unknown.
     */
    off_t remaining;

    struct strmap *headers;

#ifndef NDEBUG
    bool finished;
#endif
};

void
cgi_parser_init(struct pool *pool, CGIParser *parser);

/**
 * Did the parser finish reading the response headers?
 */
static inline bool
cgi_parser_headers_finished(const CGIParser *parser)
{
    assert(parser->finished == (parser->headers == NULL));

    return parser->headers == NULL;
}

/**
 * Run the CGI response header parser with data from the specified
 * buffer.
 *
 * @param buffer a buffer containing data received from the CGI
 * program; consumed data will automatically be removed
 * @return C_DONE when the headers are finished (the remaining buffer
 * contains the response body); C_PARTIAL or C_NONE when more header
 * data is expected; C_ERROR on error
 */
enum completion
cgi_parser_feed_headers(struct pool *pool, CGIParser *parser,
                        struct fifo_buffer *buffer, GError **error_r);

static inline http_status_t
cgi_parser_get_status(const CGIParser *parser)
{
    assert(parser != NULL);
    assert(parser->finished);

    return parser->status;
}

static inline struct strmap *
cgi_parser_get_headers(CGIParser *parser)
{
    assert(parser != NULL);
    assert(parser->headers != NULL);
    assert(parser->finished);

    struct strmap *headers = parser->headers;
    parser->headers = NULL;
    return headers;
}

static inline bool
cgi_parser_known_length(const CGIParser *parser)
{
    return parser->remaining >= 0;
}

static inline off_t
cgi_parser_available(const CGIParser *parser)
{
    return parser->remaining;
}

static inline bool
cgi_parser_requires_more(const CGIParser *parser)
{
    return parser->remaining > 0;
}

static inline bool
cgi_parser_is_too_much(const CGIParser *parser, size_t length)
{
    return parser->remaining != -1 && (off_t)length > parser->remaining;
}

/**
 * The caller has consumed data from the response body.
 *
 * @return true if the response body is finished
 */
static inline bool
cgi_parser_body_consumed(CGIParser *parser, size_t nbytes)
{
    assert(nbytes > 0);

    if (parser->remaining < 0)
        return false;

    assert((off_t)nbytes <= parser->remaining);

    parser->remaining -= nbytes;
    return parser->remaining == 0;
}

static inline bool
cgi_parser_eof(const CGIParser *parser)
{
    return parser->remaining == 0;
}

#endif
