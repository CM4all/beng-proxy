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
#include "http_headers.hxx"
#include "http_upgrade.hxx"
#include "direct.hxx"
#include "header_writer.hxx"
#include "GrowingBuffer.hxx"
#include "istream_gb.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_chunked.hxx"
#include "istream/istream_dechunk.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_string.hxx"
#include "http/Date.hxx"
#include "util/DecimalFormat.h"

#include <string.h>

bool
HttpServerConnection::MaybeSend100Continue()
{
    assert(IsValid());
    assert(request.read_state == Request::BODY);

    if (!request.expect_100_continue)
        return true;

    assert(!response.istream.IsDefined());

    request.expect_100_continue = false;

    /* this string is simple enough to expect that we don't need to
       check for partial writes, not before we have sent a single byte
       of response to the peer */
    static const char *const response_string = "HTTP/1.1 100 Continue\r\n\r\n";
    const size_t length = strlen(response_string);
    ssize_t nbytes = socket.Write(response_string, length);
    if (gcc_likely(nbytes == (ssize_t)length))
        return true;

    if (nbytes == WRITE_ERRNO)
        SocketErrorErrno("write error");
    else if (nbytes != WRITE_DESTROYED)
        SocketError("write error");
    return false;
}

static size_t
format_status_line(char *p, http_status_t status)
{
    assert(http_status_is_valid(status));

    const char *status_string = http_status_to_string(status);
    assert(status_string != nullptr);
    size_t length = strlen(status_string);

    memcpy(p, "HTTP/1.1 ", 9);
    memcpy(p + 9, status_string, length);
    length += 9;
    p[length++] = '\r';
    p[length++] = '\n';

    return length;
}

inline void
HttpServerConnection::SubmitResponse(http_status_t status,
                                     HttpHeaders &&headers,
                                     UnusedIstreamPtr _body)
{
    assert(http_status_is_valid(status));
    assert(score != HTTP_SERVER_NEW);
    assert(socket.IsConnected());
    assert(request.read_state == Request::END ||
           request.body_state == Request::BodyState::READING);

    if (http_status_is_success(status)) {
        if (score == HTTP_SERVER_FIRST)
            score = HTTP_SERVER_SUCCESS;
    } else {
        score = HTTP_SERVER_ERROR;
    }

    if (request.read_state == HttpServerConnection::Request::BODY &&
        /* if we didn't send "100 Continue" yet, we should do it now;
           we don't know if the request body will be used, but at
           least it hasn't been closed yet */
        !MaybeSend100Continue())
        return;

    auto &request_pool = request.request->pool;

    response.status = status;
    auto status_stream
        = istream_memory_new(request_pool,
                             response.status_buffer,
                             format_status_line(response.status_buffer,
                                                status));

    /* how will we transfer the body?  determine length and
       transfer-encoding */

    const bool got_body = _body;
    const off_t content_length = got_body ? _body.GetAvailable(false) : 0;
    if (http_method_is_empty(request.request->method))
        _body.Clear();

    auto *body = _body.Steal();
    if (content_length == (off_t)-1) {
        /* the response length is unknown yet */
        assert(!http_status_is_empty(status));

        if (body != nullptr && keep_alive) {
            /* keep-alive is enabled, which means that we have to
               enable chunking */
            headers.Write("transfer-encoding", "chunked");

            /* optimized code path: if an istream_dechunked shall get
               chunked via istream_chunk, let's just skip both to
               reduce the amount of work and I/O we have to do */
            if (!istream_dechunk_check_verbatim(*body))
                body = istream_chunked_new(request_pool, *body);
        }
    } else if (http_status_is_empty(status)) {
        assert(content_length == 0);
    } else if (got_body) {
        /* fixed body size */
        format_uint64(response.content_length_buffer, content_length);
        headers.Write("content-length", response.content_length_buffer);
    }

    const bool upgrade = body != nullptr && http_is_upgrade(status, headers);
    if (upgrade) {
        headers.Write("connection", "upgrade");
        headers.MoveToBuffer("upgrade");
    } else if (!keep_alive && !request.http_1_0)
        headers.Write("connection", "close");

    GrowingBuffer headers3 = headers.ToBuffer();
    headers3.Write("\r\n", 2);
    auto header_stream = istream_gb_new(request_pool, std::move(headers3));

    response.length = - status_stream.GetAvailable(false)
        - header_stream.GetAvailable(false);

    /* make sure the access logger gets a negative value if there
       is no response body */
    response.length -= body == nullptr;

    SetResponseIstream(istream_cat_new(request_pool, std::move(status_stream),
                                       std::move(header_stream),
                                       UnusedIstreamPtr(body)));
    TryWrite();
}

void
http_server_response(const HttpServerRequest *request,
                     http_status_t status,
                     HttpHeaders &&headers,
                     UnusedIstreamPtr body)
{
    auto &connection = request->connection;
    assert(connection.request.request == request);

    connection.SubmitResponse(status, std::move(headers), std::move(body));
}

void
http_server_simple_response(const HttpServerRequest &request,
                            http_status_t status, const char *location,
                            const char *msg)
{
    assert(unsigned(status) >= 200 && unsigned(status) < 600);

    if (http_status_is_empty(status))
        msg = nullptr;
    else if (msg == nullptr)
        msg = http_status_to_string(status);

    HttpHeaders headers(request.pool);

#ifndef NO_DATE_HEADER
    headers.Write("date", http_date_format(std::chrono::system_clock::now()));
#endif

    if (location != nullptr)
        headers.Write("location", location);

    UnusedIstreamPtr body;
    if (msg != nullptr) {
        headers.Write("content-type", "text/plain");
        body = istream_string_new(request.pool, msg);
    }

    http_server_response(&request, status, std::move(headers),
                         std::move(body));
}

void
http_server_send_message(const HttpServerRequest *request,
                         http_status_t status, const char *msg)
{
    HttpHeaders headers(request->pool);

    headers.Write("content-type", "text/plain");

#ifndef NO_DATE_HEADER
    headers.Write("date", http_date_format(std::chrono::system_clock::now()));
#endif

    http_server_response(request, status, std::move(headers),
                         istream_string_new(request->pool, msg));
}

void
http_server_send_redirect(const HttpServerRequest *request,
                          http_status_t status, const char *location,
                          const char *msg)
{
    assert(request != nullptr);
    assert(status >= 300 && status < 400);
    assert(location != nullptr);

    if (msg == nullptr)
        msg = "redirection";

    HttpHeaders headers(request->pool);

    headers.Write("content-type", "text/plain");
    headers.Write("location", location);

#ifndef NO_DATE_HEADER
    headers.Write("date", http_date_format(std::chrono::system_clock::now()));
#endif

    http_server_response(request, status, std::move(headers),
                         istream_string_new(request->pool, msg));
}
