/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_server_internal.hxx"
#include "http_headers.hxx"
#include "http_upgrade.hxx"
#include "direct.h"
#include "header_writer.hxx"
#include "format.h"
#include "date.h"
#include "growing_buffer.hxx"
#include "istream_gb.hxx"

#include <string.h>

bool
http_server_connection::MaybeSend100Continue()
{
    assert(IsValid());
    assert(request.read_state == Request::BODY);

    if (!request.expect_100_continue)
        return true;

    assert(response.istream == nullptr);

    request.expect_100_continue = false;

    /* this string is simple enough to expect that we don't need to
       check for partial writes, not before we have sent a single byte
       of response to the peer */
    static const char *const response = "HTTP/1.1 100 Continue\r\n\r\n";
    const size_t length = strlen(response);
    ssize_t nbytes = socket.Write(response, length);
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

void
http_server_response(const struct http_server_request *request,
                     http_status_t status,
                     HttpHeaders &&headers,
                     struct istream *body)
{
    struct http_server_connection *connection = request->connection;

    assert(connection->score != HTTP_SERVER_NEW);
    assert(connection->request.request == request);
    assert(connection->socket.IsConnected());

    connection->request.async_ref.Poison();

    if (http_status_is_success(status)) {
        if (connection->score == HTTP_SERVER_FIRST)
            connection->score = HTTP_SERVER_SUCCESS;
    } else {
        connection->score = HTTP_SERVER_ERROR;
    }

    if (connection->request.read_state == http_server_connection::Request::BODY &&
        /* if we didn't send "100 Continue" yet, we should do it now;
           we don't know if the request body will be used, but at
           least it hasn't been closed yet */
        !connection->MaybeSend100Continue())
        return;

    connection->response.status = status;
    struct istream *status_stream
        = istream_memory_new(request->pool,
                             connection->response.status_buffer,
                             format_status_line(connection->response.status_buffer,
                                                status));

    struct growing_buffer &headers2 = headers.MakeBuffer(*request->pool, 256);

    /* how will we transfer the body?  determine length and
       transfer-encoding */

    const off_t content_length = body == nullptr
        ? 0 : istream_available(body, false);
    if (content_length == (off_t)-1) {
        /* the response length is unknown yet */
        assert(!http_status_is_empty(status));

        if (!http_method_is_empty(request->method) && connection->keep_alive) {
            /* keep-alive is enabled, which means that we have to
               enable chunking */
            header_write(&headers2, "transfer-encoding", "chunked");
            body = istream_chunked_new(request->pool, body);
        }
    } else if (http_status_is_empty(status)) {
        assert(content_length == 0);
    } else if (body != nullptr || !http_method_is_empty(request->method)) {
        /* fixed body size */
        format_uint64(connection->response.content_length_buffer, content_length);
        header_write(&headers2, "content-length",
                     connection->response.content_length_buffer);
    }

    if (http_method_is_empty(request->method) && body != nullptr)
        istream_free_unused(&body);

    const bool upgrade = body != nullptr && http_is_upgrade(status, headers);
    if (upgrade) {
        headers.Write(*request->pool, "connection", "upgrade");
        headers.MoveToBuffer(*request->pool, "upgrade");
    } else if (!connection->keep_alive && !connection->request.http_1_0)
        header_write(&headers2, "connection", "close");

    struct growing_buffer &headers3 = headers.ToBuffer(*request->pool);
    growing_buffer_write_buffer(&headers3, "\r\n", 2);
    struct istream *header_stream = istream_gb_new(request->pool, &headers3);

    connection->response.length = - istream_available(status_stream, false)
        - istream_available(header_stream, false);

    body = istream_cat_new(request->pool, status_stream,
                           header_stream, body, nullptr);

    connection->response.istream = body;
    istream_handler_set(connection->response.istream,
                        &http_server_response_stream_handler, connection,
                        connection->socket.GetDirectMask());

    connection->socket.SetCork(true);
    if (connection->TryWrite())
        connection->socket.SetCork(false);
}

void
http_server_send_message(const struct http_server_request *request,
                         http_status_t status, const char *msg)
{
    HttpHeaders headers;
    struct growing_buffer &headers2 = headers.MakeBuffer(*request->pool, 256);

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
    struct growing_buffer &headers2 = headers.MakeBuffer(*request->pool, 1024);

    header_write(&headers2, "content-type", "text/plain");
    header_write(&headers2, "location", location);

#ifndef NO_DATE_HEADER
    header_write(&headers2, "date", http_date_format(time(nullptr)));
#endif

    http_server_response(request, status, std::move(headers),
                         istream_string_new(request->pool, msg));
}
