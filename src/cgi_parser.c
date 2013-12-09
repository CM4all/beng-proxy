/*
 * Parsing CGI responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi_parser.h"
#include "cgi_quark.h"
#include "fifo-buffer.h"
#include "strmap.h"
#include "header-parser.h"
#include "strutil.h"

#include <string.h>
#include <stdlib.h>

void
cgi_parser_init(struct pool *pool, struct cgi_parser *parser)
{
    parser->status = HTTP_STATUS_OK;
    parser->remaining = -1;
    parser->headers = strmap_new(pool, 31);

#ifndef NDEBUG
    parser->finished = false;
#endif
}

/**
 * Evaluate the response headers after the headers have been finalized
 * by an empty line.
 */
static enum completion
cgi_parser_finish(struct cgi_parser *parser, struct fifo_buffer *buffer,
                  GError **error_r)
{
    /* parse the status */
    const char *p = strmap_remove(parser->headers, "status");
    if (p != NULL) {
        int i = atoi(p);
        if (http_status_is_valid(i))
            parser->status = (http_status_t)i;
    }

    if (http_status_is_empty(parser->status)) {
        /* there cannot be a response body */
        parser->remaining = 0;
    } else {
        p = strmap_remove(parser->headers, "content-length");
        if (p != NULL) {
            /* parse the Content-Length response header */
            char *endptr;
            parser->remaining = (off_t)strtoull(p, &endptr, 10);
            if (endptr == p || *endptr != 0 || parser->remaining < 0)
                parser->remaining = -1;
        } else
            /* unknown length */
            parser->remaining = -1;
    }

    if (cgi_parser_is_too_much(parser, fifo_buffer_available(buffer))) {
        g_set_error(error_r, cgi_quark(), 0, "too much data from CGI script");
        return C_ERROR;
    }

#ifndef NDEBUG
    parser->finished = true;
#endif
    return C_DONE;
}

enum completion
cgi_parser_feed_headers(struct pool *pool, struct cgi_parser *parser,
                        struct fifo_buffer *buffer, GError **error_r)
{
    assert(!cgi_parser_headers_finished(parser));

    size_t length;
    const char *data = fifo_buffer_read(buffer, &length);
    if (data == NULL)
        return C_MORE;

    assert(length > 0);
    const char *data_end = data + length;

    /* parse each line until we stumble upon an empty one */
    const char *start = data, *end, *next = NULL;
    while ((end = memchr(start, '\n', data_end - start)) != NULL) {
        next = end + 1;
        --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        const size_t line_length = end - start + 1;
        if (line_length == 0) {
            /* found an empty line, which is the separator between
               headers and body */
            fifo_buffer_consume(buffer, next - data);
            return cgi_parser_finish(parser, buffer, error_r);
        }

        header_parse_line(pool, parser->headers, start, line_length);

        start = next;
    }

    if (next != NULL) {
        fifo_buffer_consume(buffer, next - data);
        return C_MORE;
    }

    if (fifo_buffer_full(buffer)) {
        /* the buffer is full, and no header could be parsed: this
           means the current header is too large for the buffer; bail
           out */

        g_set_error(error_r, cgi_quark(), 0, "CGI response header too long");
        return C_ERROR;
    }

    return C_MORE;
}
