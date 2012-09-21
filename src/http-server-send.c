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
#include "istream-gb.h"

#include <string.h>

bool
http_server_maybe_send_100_continue(struct http_server_connection *connection)
{
    assert(http_server_connection_valid(connection));
    assert(connection->request.read_state == READ_BODY);

    if (!connection->request.expect_100_continue)
        return true;

    assert(connection->response.istream == NULL);

    connection->request.expect_100_continue = false;

    connection->response.istream = istream_string_new(connection->request.request->pool,
                                                      "HTTP/1.1 100 Continue\r\n\r\n");
    istream_handler_set(connection->response.istream,
                        &http_server_response_stream_handler, connection,
                        socket_wrapper_direct_mask(&connection->socket));

    connection->response.writing_100_continue = true;

    return http_server_try_write(connection);
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
http_server_response(const struct http_server_request *request,
                     http_status_t status,
                     struct growing_buffer *headers,
                     istream_t body)
{
    struct http_server_connection *connection = request->connection;
    off_t content_length;
    istream_t status_stream, header_stream;

    assert(connection->score != HTTP_SERVER_NEW);
    assert(connection->request.request == request);

    /* clear the reference, because http_server_request_close() needs
       this to check if the response has been sent already */
    async_ref_clear(&connection->request.async_ref);

    if (http_status_is_success(status)) {
        if (connection->score == HTTP_SERVER_FIRST)
            connection->score = HTTP_SERVER_SUCCESS;
    } else {
        connection->score = HTTP_SERVER_ERROR;
    }

    if (connection->request.read_state == READ_BODY &&
        /* if we didn't send "100 Continue" yet, we should do it now;
           we don't know if the request body will be used, but at
           least it hasn't been closed yet */
        !http_server_maybe_send_100_continue(connection))
        return;

    connection->response.status = status;
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

    /* how will we transfer the body?  determine length and
       transfer-encoding */

    content_length = body == NULL
        ? 0 : istream_available(body, false);
    if (content_length == (off_t)-1) {
        /* the response length is unknown yet */
        assert(!http_status_is_empty(status));

        if (!http_method_is_empty(request->method) && connection->keep_alive) {
            /* keep-alive is enabled, which means that we have to
               enable chunking */
            header_write(headers, "transfer-encoding", "chunked");
            body = istream_chunked_new(request->pool, body);
        }
    } else if (http_status_is_empty(status)) {
        assert(content_length == 0);
    } else if (body != NULL || !http_method_is_empty(request->method)) {
        /* fixed body size */
        format_uint64(connection->response.content_length_buffer, content_length);
        header_write(headers, "content-length",
                     connection->response.content_length_buffer);
    }

    if (http_method_is_empty(request->method) && body != NULL)
        istream_free_unused(&body);

    if (!connection->keep_alive && !connection->request.http_1_0)
        header_write(headers, "connection", "close");

    growing_buffer_write_buffer(headers, "\r\n", 2);

    header_stream = istream_gb_new(request->pool, headers);

    connection->response.length = - istream_available(status_stream, false)
        - istream_available(header_stream, false);

    body = istream_cat_new(request->pool, status_stream,
                           header_stream, body, NULL);

    if (connection->response.istream != NULL) {
        /* we havn't yet finished writing "100 Continue" yet;
           concatenate this stream and the response stream now  */
        assert(connection->response.writing_100_continue);

        connection->response.writing_100_continue = false;

        istream_handler_clear(connection->response.istream);
        connection->response.length -=
            istream_available(connection->response.istream, false);

        body = istream_cat_new(request->pool, connection->response.istream, body);
    }

    connection->response.istream = body;
    istream_handler_set(connection->response.istream,
                        &http_server_response_stream_handler, connection,
                        socket_wrapper_direct_mask(&connection->socket));

    connection->response.writing_100_continue = false;

    socket_wrapper_set_cork(&connection->socket, true);
    if (http_server_try_write(connection))
        socket_wrapper_set_cork(&connection->socket, false);
}

void
http_server_send_message(const struct http_server_request *request,
                         http_status_t status, const char *msg)
{
    struct growing_buffer *headers = growing_buffer_new(request->pool, 256);
    header_write(headers, "content-type", "text/plain");

    http_server_response(request, status, headers,
                         istream_string_new(request->pool, msg));
}

void
http_server_send_redirect(const struct http_server_request *request,
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
    header_write(headers, "content-type", "text/plain");
    header_write(headers, "location", location);

    http_server_response(request, status, headers,
                         istream_string_new(request->pool, msg));
}
