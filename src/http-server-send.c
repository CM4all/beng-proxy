/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "direct.h"
#include "header-writer.h"
#include "format.h"
#include "date.h"
#include "growing-buffer.h"

#include <string.h>

void
http_server_maybe_send_100_continue(http_server_connection_t connection)
{
    assert(connection->fd >= 0);
    assert(connection->request.read_state == READ_BODY);

    if (!connection->request.expect_100_continue)
        return;

    assert(connection->response.istream == NULL);

    connection->request.expect_100_continue = false;

    connection->response.istream = istream_string_new(connection->request.request->pool,
                                                      "100 Continue\r\n\r\n");
    istream_handler_set(connection->response.istream,
                        &http_server_response_stream_handler, connection,
                        ISTREAM_DIRECT_SUPPORT);

    connection->response.writing_100_continue = true;

    http_server_try_write(connection);
}

static size_t
format_status_line(char *p, http_status_t status)
{
    const char *status_string;
    size_t length;

    assert(status >= 100 && status < 600);

    status_string = http_status_to_string(status);
    assert(status_string != NULL);
    length = strlen(status_string);

    memcpy(p, "HTTP/1.1 ", 9);
    memcpy(p + 9, status_string, length);
    length += 9;
    p[length++] = '\r';
    p[length++] = '\n';

    return length;
}

void
http_server_response(struct http_server_request *request,
                     http_status_t status,
                     struct growing_buffer *headers,
                     istream_t body)
{
    http_server_connection_t connection = request->connection;
    off_t content_length;
    istream_t status_stream, header_stream;

    assert(connection->request.request == request);
    assert(connection->response.istream == NULL);
    /* XXX what if we weren't able to send "100 Continue" yet? */

    async_ref_poison(&connection->request.async_ref);

    status_stream
        = istream_memory_new(request->pool,
                             connection->response.status_buffer,
                             format_status_line(connection->response.status_buffer,
                                                status));

    if (headers == NULL)
        headers = growing_buffer_new(request->pool, 256);

#ifndef NO_DATE_HEADER
    header_write(headers, "date", http_date_format(time(NULL)));
#endif

    content_length = body == NULL
        ? 0 : istream_available(body, false);
    if (content_length == (off_t)-1) {
        assert(!http_status_is_empty(status));

        if (body != NULL && connection->keep_alive) {
            header_write(headers, "transfer-encoding", "chunked");
            body = istream_chunked_new(request->pool, body);
        }
    } else if (http_status_is_empty(status)) {
        assert(content_length == 0);
    } else {
        format_uint64(connection->response.content_length_buffer, content_length);
        header_write(headers, "content-length",
                     connection->response.content_length_buffer);
    }

    if (request->method == HTTP_METHOD_HEAD && body != NULL)
        istream_free(&body);

#ifdef __linux
#ifdef SPLICE
    if (body != NULL)
        body = istream_pipe_new(request->pool, body);
#endif
#endif

    header_write(headers, "connection",
                 connection->keep_alive ? "keep-alive" : "close");

    growing_buffer_write_buffer(headers, "\r\n", 2);

    header_stream = growing_buffer_istream(headers);

    connection->response.istream = istream_cat_new(request->pool, status_stream,
                                                   header_stream, body, NULL);
    istream_handler_set(connection->response.istream,
                        &http_server_response_stream_handler, connection,
                        ISTREAM_DIRECT_SUPPORT);

    connection->response.writing_100_continue = false;

    pool_ref(connection->pool);
    http_server_try_write(connection);
    pool_unref(connection->pool);
}

void
http_server_send_message(struct http_server_request *request,
                         http_status_t status, const char *msg)
{
    struct growing_buffer *headers = growing_buffer_new(request->pool, 40);
    header_write(headers, "content-type", "text/plain");

    http_server_response(request, status, headers,
                         istream_string_new(request->pool, msg));
}

void
http_server_send_redirect(struct http_server_request *request,
                          http_status_t status, const char *location,
                          const char *msg)
{
    struct growing_buffer *headers;

    assert(request != NULL);
    assert(status >= 300 && status < 400);
    assert(location != NULL);

    if (msg == NULL)
        msg = "redirection";

    headers = growing_buffer_new(request->pool, 1024);
    header_write(headers, "location", location);

    http_server_response(request, status, headers,
                         istream_string_new(request->pool, msg));
}
