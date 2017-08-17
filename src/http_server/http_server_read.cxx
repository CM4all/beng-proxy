/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Internal.hxx"
#include "Request.hxx"
#include "Handler.hxx"
#include "http_upgrade.hxx"
#include "pool.hxx"
#include "strmap.hxx"
#include "header_parser.hxx"
#include "istream/istream_null.hxx"
#include "http/List.hxx"
#include "util/StringUtil.hxx"
#include "util/StringView.hxx"

#include <daemon/log.h>

#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>

inline bool
HttpServerConnection::ParseRequestLine(const char *line, size_t length)
{
    assert(request.read_state == Request::START);
    assert(request.request == nullptr);
    assert(!response.pending_drained);

    if (unlikely(length < 5)) {
        ProtocolError("malformed request line");
        return false;
    }

    const char *eol = line + length;

    http_method_t method = HTTP_METHOD_NULL;
    switch (line[0]) {
    case 'D':
        if (likely(line[1] == 'E' && line[2] == 'L' && line[3] == 'E' &&
                   line[4] == 'T' && line[5] == 'E' && line[6] == ' ')) {
            method = HTTP_METHOD_DELETE;
            line += 7;
        }
        break;

    case 'G':
        if (likely(line[1] == 'E' && line[2] == 'T' && line[3] == ' ')) {
            method = HTTP_METHOD_GET;
            line += 4;
        }
        break;

    case 'P':
        if (likely(line[1] == 'O' && line[2] == 'S' && line[3] == 'T' &&
                   line[4] == ' ')) {
            method = HTTP_METHOD_POST;
            line += 5;
        } else if (line[1] == 'U' && line[2] == 'T' && line[3] == ' ') {
            method = HTTP_METHOD_PUT;
            line += 4;
        } else if (memcmp(line + 1, "ATCH ", 5) == 0) {
            method = HTTP_METHOD_PATCH;
            line += 6;
        } else if (memcmp(line + 1, "ROPFIND ", 8) == 0) {
            method = HTTP_METHOD_PROPFIND;
            line += 9;
        } else if (memcmp(line + 1, "ROPPATCH ", 9) == 0) {
            method = HTTP_METHOD_PROPPATCH;
            line += 10;
        }

        break;

    case 'H':
        if (likely(line[1] == 'E' && line[2] == 'A' && line[3] == 'D' &&
                   line[4] == ' ')) {
            method = HTTP_METHOD_HEAD;
            line += 5;
        }
        break;

    case 'O':
        if (memcmp(line + 1, "PTIONS ", 7) == 0) {
            method = HTTP_METHOD_OPTIONS;
            line += 8;
        }
        break;

    case 'T':
        if (memcmp(line + 1, "RACE ", 5) == 0) {
            method = HTTP_METHOD_TRACE;
            line += 6;
        }
        break;

    case 'M':
        if (memcmp(line + 1, "KCOL ", 5) == 0) {
            method = HTTP_METHOD_MKCOL;
            line += 6;
        } else if (memcmp(line + 1, "OVE ", 4) == 0) {
            method = HTTP_METHOD_MOVE;
            line += 5;
        }
        break;

    case 'C':
        if (memcmp(line + 1, "OPY ", 4) == 0) {
            method = HTTP_METHOD_COPY;
            line += 5;
        }
        break;

    case 'L':
        if (memcmp(line + 1, "OCK ", 4) == 0) {
            method = HTTP_METHOD_LOCK;
            line += 5;
        }
        break;

    case 'U':
        if (memcmp(line + 1, "NLOCK ", 6) == 0) {
            method = HTTP_METHOD_UNLOCK;
            line += 7;
        }
        break;
    }

    if (method == HTTP_METHOD_NULL) {
        /* invalid request method */

        ProtocolError("unrecognized request method");
        return false;
    }

    const char *space = (const char *)memchr(line, ' ', eol - line);
    if (unlikely(space == nullptr || space + 6 > line + length ||
                 memcmp(space + 1, "HTTP/", 5) != 0)) {
        /* refuse HTTP 0.9 requests */
        static const char msg[] =
            "This server requires HTTP 1.1.";

        ssize_t nbytes = socket.Write(msg, sizeof(msg) - 1);
        if (nbytes != WRITE_DESTROYED)
            Done();
        return false;
    }

    request.request = http_server_request_new(this, method, {line, space});
    request.read_state = Request::HEADERS;
    request.http_1_0 = space + 9 <= line + length &&
        space[8] == '0' && space[7] == '.' && space[6] == '1';

    return true;
}

/**
 * @return false if the connection has been closed
 */
inline bool
HttpServerConnection::HeadersFinished()
{
    auto &r = *request.request;

    /* disable the idle+headers timeout; the request body timeout will
       be tracked by filtered_socket (auto-refreshing) */
    idle_timeout.Cancel();

    const char *value = r.headers.Get("expect");
    request.expect_100_continue = value != nullptr &&
        strcmp(value, "100-continue") == 0;
    request.expect_failed = value != nullptr &&
        strcmp(value, "100-continue") != 0;

    value = r.headers.Get("connection");

    /* we disable keep-alive support on ancient HTTP 1.0, because that
       feature was not well-defined and led to problems with some
       clients */
    keep_alive = !request.http_1_0 &&
        (value == nullptr || !http_list_contains_i(value, "close"));

    const bool upgrade = !request.http_1_0 && value != nullptr &&
        http_is_upgrade(value);

    value = r.headers.Get("transfer-encoding");

    const struct timeval *read_timeout = &http_server_read_timeout;

    off_t content_length = -1;
    const bool chunked = value != nullptr && strcasecmp(value, "chunked") == 0;
    if (!chunked) {
        value = r.headers.Get("content-length");

        if (upgrade) {
            if (value != nullptr) {
                ProtocolError("cannot upgrade with Content-Length request header");
                return false;
            }

            /* disable timeout */
            read_timeout = nullptr;

            /* forward incoming data as-is */

            keep_alive = false;
        } else if (value == nullptr) {
            /* no body at all */

            r.body = nullptr;
            request.read_state = Request::END;

            return true;
        } else {
            char *endptr;

            content_length = strtoul(value, &endptr, 10);
            if (unlikely(*endptr != 0 || content_length < 0)) {
                ProtocolError("invalid Content-Length header in HTTP request");
                return false;
            }

            if (content_length == 0) {
                /* empty body */

                r.body = istream_null_new(&r.pool);
                request.read_state = Request::END;

                return true;
            }
        }
    } else if (upgrade) {
        ProtocolError("cannot upgrade chunked request");
        return false;
    }

    request_body_reader = NewFromPool<RequestBodyReader>(r.pool, r.pool,
                                                         *this);
    r.body = &request_body_reader->Init(GetEventLoop(), content_length,
                                        chunked);

    request.read_state = Request::BODY;

    /* for the response body, the filtered_socket class tracks
       inactivity timeout */
    socket.ScheduleReadTimeout(false, read_timeout);

    return true;
}

/**
 * @return false if the connection has been closed
 */
inline bool
HttpServerConnection::HandleLine(const char *line, size_t length)
{
    assert(request.read_state == Request::START ||
           request.read_state == Request::HEADERS);

    if (unlikely(request.read_state == Request::START)) {
        assert(request.request == nullptr);

        return ParseRequestLine(line, length);
    } else if (likely(length > 0)) {
        assert(request.read_state == Request::HEADERS);
        assert(request.request != nullptr);

        header_parse_line(request.request->pool,
                          request.request->headers,
                          {line, length});
        return true;
    } else {
        assert(request.read_state == Request::HEADERS);
        assert(request.request != nullptr);

        return HeadersFinished();
    }
}

inline BufferedResult
HttpServerConnection::FeedHeaders(const void *_data, size_t length)
{
    assert(request.read_state == Request::START ||
           request.read_state == Request::HEADERS);

    if (request.bytes_received >= 64 * 1024) {
        daemon_log(2, "http_server: too many request headers\n");
        http_server_connection_close(this);
        return BufferedResult::CLOSED;
    }

    const char *const buffer = (const char *)_data;
    const char *const buffer_end = buffer + length;
    const char *start = buffer, *end, *next = nullptr;
    while ((end = (const char *)memchr(start, '\n',
                                       buffer_end - start)) != nullptr) {
        next = end + 1;

        end = StripRight(start, end);

        if (!HandleLine(start, end - start))
            return BufferedResult::CLOSED;

        if (request.read_state != Request::HEADERS)
            break;

        start = next;
    }

    size_t consumed = 0;
    if (next != nullptr) {
        consumed = next - buffer;
        request.bytes_received += consumed;
        socket.Consumed(consumed);
    }

    return request.read_state == Request::HEADERS
        ? BufferedResult::MORE
        : (consumed == length ? BufferedResult::OK : BufferedResult::PARTIAL);
}

inline bool
HttpServerConnection::SubmitRequest()
{
    if (request.read_state == Request::END)
        /* re-enable the event, to detect client disconnect while
           we're processing the request */
        socket.ScheduleReadNoTimeout(false);

    const ScopePoolRef ref(*pool TRACE_ARGS);

    if (request.expect_failed)
        http_server_send_message(request.request,
                                 HTTP_STATUS_EXPECTATION_FAILED,
                                 "Unrecognized expectation");
    else {
        request.in_handler = true;
        handler->HandleHttpRequest(*request.request, request.cancel_ptr);
        request.in_handler = false;
    }

    return IsValid();
}

BufferedResult
HttpServerConnection::Feed(const void *data, size_t length)
{
    assert(!response.pending_drained);

    switch (request.read_state) {
        BufferedResult result;

    case Request::START:
        if (score == HTTP_SERVER_NEW)
            score = HTTP_SERVER_FIRST;

    case Request::HEADERS:
        result = FeedHeaders(data, length);
        if ((result == BufferedResult::OK || result == BufferedResult::PARTIAL) &&
            (request.read_state == Request::BODY ||
             request.read_state == Request::END)) {
            if (request.read_state == Request::BODY)
                result = request_body_reader->RequireMore()
                    ? BufferedResult::AGAIN_EXPECT
                    : BufferedResult::AGAIN_OPTIONAL;

            if (!SubmitRequest())
                result = BufferedResult::CLOSED;
        }

        return result;

    case Request::BODY:
        return FeedRequestBody(data, length);

    case Request::END:
        /* check if the connection was closed by the client while we
           were processing the request */

        if (socket.IsFull())
            /* the buffer is full, the peer has been pipelining too
               much - that would disallow us to detect a disconnect;
               let's disable keep-alive now and discard all data */
            keep_alive = false;

        if (!keep_alive) {
            /* discard all pipelined input when keep-alive has been
               disabled */
            socket.Consumed(length);
            return BufferedResult::OK;
        }

        return BufferedResult::MORE;
    }

    assert(false);
    gcc_unreachable();
}

DirectResult
HttpServerConnection::TryRequestBodyDirect(int fd,
                                             FdType fd_type)
{
    assert(IsValid());
    assert(request.read_state == Request::BODY);
    assert(!response.pending_drained);

    if (!MaybeSend100Continue())
        return DirectResult::CLOSED;

    ssize_t nbytes = request_body_reader->TryDirect(fd, fd_type);
    if (nbytes == ISTREAM_RESULT_BLOCKING)
        /* the destination fd blocks */
        return DirectResult::BLOCKING;

    if (nbytes == ISTREAM_RESULT_CLOSED)
        /* the stream (and the whole connection) has been closed
           during the direct() callback (-3); no further checks */
        return DirectResult::CLOSED;

    if (nbytes < 0) {
        if (errno == EAGAIN)
            return DirectResult::EMPTY;

        return DirectResult::ERRNO;
    }

    if (nbytes == ISTREAM_RESULT_EOF)
        return DirectResult::END;

    request.bytes_received += nbytes;

    if (request_body_reader->IsEOF()) {
        request.read_state = Request::END;
        request_body_reader->DestroyEof();
        return IsValid()
            ? DirectResult::OK
            : DirectResult::CLOSED;
    } else {
        return DirectResult::OK;
    }
}

void
HttpServerConnection::OnDeferredRead()
{
    socket.Read(false);
}
