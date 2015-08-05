/*
 * Parsing CGI responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi_parser.hxx"
#include "cgi_quark.h"
#include "strmap.hxx"
#include "header_parser.hxx"
#include "util/ForeignFifoBuffer.hxx"
#include "util/CharUtil.hxx"

#include <string.h>
#include <stdlib.h>

CGIParser::CGIParser(struct pool &pool)
    :status(HTTP_STATUS_OK),
     remaining(-1),
     headers(strmap_new(&pool))
#ifndef NDEBUG
    , finished(false)
#endif
{
}

/**
 * Evaluate the response headers after the headers have been finalized
 * by an empty line.
 */
inline enum completion
CGIParser::Finish(ForeignFifoBuffer<uint8_t> &buffer, GError **error_r)
{
    /* parse the status */
    const char *p = headers->Remove("status");
    if (p != nullptr) {
        int i = atoi(p);
        if (http_status_is_valid((http_status_t)i))
            status = (http_status_t)i;
    }

    if (http_status_is_empty(status)) {
        /* there cannot be a response body */
        remaining = 0;
    } else {
        p = headers->Remove("content-length");
        if (p != nullptr) {
            /* parse the Content-Length response header */
            char *endptr;
            remaining = (off_t)strtoull(p, &endptr, 10);
            if (endptr == p || *endptr != 0 || remaining < 0)
                remaining = -1;
        } else
            /* unknown length */
            remaining = -1;
    }

    if (IsTooMuch(buffer.GetAvailable())) {
        g_set_error(error_r, cgi_quark(), 0, "too much data from CGI script");
        return C_ERROR;
    }

#ifndef NDEBUG
    finished = true;
#endif
    return C_DONE;
}

enum completion
CGIParser::FeedHeaders(struct pool &pool, ForeignFifoBuffer<uint8_t> &buffer,
                       GError **error_r)
{
    assert(!AreHeadersFinished());

    auto r = buffer.Read();
    if (r.IsEmpty())
        return C_MORE;

    const char *data = (const char *)r.data;
    const char *data_end = data + r.size;

    /* parse each line until we stumble upon an empty one */
    const char *start = data, *end, *next = nullptr;
    while ((end = (const char *)memchr(start, '\n',
                                       data_end - start)) != nullptr) {
        next = end + 1;
        --end;
        while (end >= start && IsWhitespaceOrNull(*end))
            --end;

        const size_t line_length = end - start + 1;
        if (line_length == 0) {
            /* found an empty line, which is the separator between
               headers and body */
            buffer.Consume(next - data);
            return Finish(buffer, error_r);
        }

        header_parse_line(&pool, headers, start, line_length);

        start = next;
    }

    if (next != nullptr) {
        buffer.Consume(next - data);
        return C_MORE;
    }

    if (buffer.IsFull()) {
        /* the buffer is full, and no header could be parsed: this
           means the current header is too large for the buffer; bail
           out */

        g_set_error(error_r, cgi_quark(), 0, "CGI response header too long");
        return C_ERROR;
    }

    return C_MORE;
}
