/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Internal.hxx"
#include "Request.hxx"
#include "http_headers.hxx"
#include "http_upgrade.hxx"
#include "direct.hxx"
#include "header_writer.hxx"
#include "format.h"
#include "date.h"
#include "growing_buffer.hxx"
#include "istream_gb.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_chunked.hxx"
#include "istream/istream_dechunk.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_string.hxx"

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
        ErrorErrno("write error");
    else if (nbytes != WRITE_DESTROYED)
        Error("write error");
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
                                     Istream *body)
{
    assert(score != HTTP_SERVER_NEW);
    assert(socket.IsConnected());

    request.async_ref.Poison();

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

    struct pool &request_pool = *request.request->pool;

    response.status = status;
    Istream *status_stream
        = istream_memory_new(&request_pool,
                             response.status_buffer,
                             format_status_line(response.status_buffer,
                                                status));

    GrowingBuffer &headers2 = headers.MakeBuffer(request_pool, 256);

    /* how will we transfer the body?  determine length and
       transfer-encoding */

    const off_t content_length = body == nullptr
        ? 0 : body->GetAvailable(false);
    if (content_length == (off_t)-1) {
        /* the response length is unknown yet */
        assert(!http_status_is_empty(status));

        if (!http_method_is_empty(request.request->method) && keep_alive) {
            /* keep-alive is enabled, which means that we have to
               enable chunking */
            header_write(&headers2, "transfer-encoding", "chunked");

            /* optimized code path: if an istream_dechunked shall get
               chunked via istream_chunk, let's just skip both to
               reduce the amount of work and I/O we have to do */
            if (!istream_dechunk_check_verbatim(*body))
                body = istream_chunked_new(request_pool, *body);
        }
    } else if (http_status_is_empty(status)) {
        assert(content_length == 0);
    } else if (body != nullptr || !http_method_is_empty(request.request->method)) {
        /* fixed body size */
        format_uint64(response.content_length_buffer, content_length);
        header_write(&headers2, "content-length",
                     response.content_length_buffer);
    }

    if (http_method_is_empty(request.request->method) && body != nullptr)
        istream_free_unused(&body);

    const bool upgrade = body != nullptr && http_is_upgrade(status, headers);
    if (upgrade) {
        headers.Write(request_pool, "connection", "upgrade");
        headers.MoveToBuffer(request_pool, "upgrade");
    } else if (!keep_alive && !request.http_1_0)
        header_write(&headers2, "connection", "close");

    GrowingBuffer &headers3 = headers.ToBuffer(request_pool);
    growing_buffer_write_buffer(&headers3, "\r\n", 2);
    Istream *header_stream = istream_gb_new(request_pool, headers3);

    response.length = - status_stream->GetAvailable(false)
        - header_stream->GetAvailable(false);

    body = istream_cat_new(request_pool, status_stream,
                           header_stream, body);

    SetResponseIstream(*body);
    TryWrite();
}

void
http_server_response(const struct http_server_request *request,
                     http_status_t status,
                     HttpHeaders &&headers,
                     Istream *body)
{
    auto *connection = request->connection;
    assert(connection->request.request == request);

    connection->SubmitResponse(status, std::move(headers), body);
}

void
http_server_send_message(const struct http_server_request *request,
                         http_status_t status, const char *msg)
{
    HttpHeaders headers;
    GrowingBuffer &headers2 = headers.MakeBuffer(*request->pool, 256);

    header_write(&headers2, "content-type", "text/plain");

#ifndef NO_DATE_HEADER
    header_write(&headers2, "date", http_date_format(time(nullptr)));
#endif

    http_server_response(request, status, std::move(headers),
                         istream_string_new(request->pool, msg));
}

void
http_server_send_redirect(const struct http_server_request *request,
                          http_status_t status, const char *location,
                          const char *msg)
{
    assert(request != nullptr);
    assert(status >= 300 && status < 400);
    assert(location != nullptr);

    if (msg == nullptr)
        msg = "redirection";

    HttpHeaders headers;
    GrowingBuffer &headers2 = headers.MakeBuffer(*request->pool, 1024);

    header_write(&headers2, "content-type", "text/plain");
    header_write(&headers2, "location", location);

#ifndef NO_DATE_HEADER
    header_write(&headers2, "date", http_date_format(time(nullptr)));
#endif

    http_server_response(request, status, std::move(headers),
                         istream_string_new(request->pool, msg));
}
