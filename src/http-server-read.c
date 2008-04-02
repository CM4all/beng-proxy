/*
 * HTTP server implementation.
 *
 * istream implementation for the request body.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "strutil.h"
#include "buffered-io.h"
#include "header-parser.h"
#include "istream-internal.h"

#include <daemon/log.h>

#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static void
http_server_parse_request_line(http_server_connection_t connection,
                               const char *line, size_t length)
{
    const char *eol, *space;
    http_method_t method = HTTP_METHOD_NULL;

    assert(connection != NULL);
    assert(connection->request.read_state == READ_START);
    assert(connection->request.request == NULL);

    if (unlikely(length < 5)) {
        http_server_connection_close(connection);
        return;
    }

    eol = line + length;

    switch (line[0]) {
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
        }

        if (line[1] == 'U' && line[2] == 'T' && line[3] == ' ') {
            method = HTTP_METHOD_PUT;
            line += 4;
        }
        break;

    case 'H':
        if (likely(line[1] == 'E' && line[2] == 'A' && line[3] == 'D' &&
                   line[4] == ' ')) {
            method = HTTP_METHOD_HEAD;
            line += 5;
        }
        break;
    }

    /* XXX: unknown method? */

    space = memchr(line, ' ', eol - line);
    if (unlikely(space == NULL))
        space = eol;

    connection->request.request = http_server_request_new(connection);
    connection->request.request->method = method;
    connection->request.request->uri = p_strndup(connection->request.request->pool, line, space - line);
    connection->request.read_state = READ_HEADERS;
}

static void
http_server_headers_finished(http_server_connection_t connection)
{
    struct http_server_request *request = connection->request.request;
    const char *value;
    off_t content_length;

    value = strmap_get(request->headers, "connection");
    connection->keep_alive = value != NULL &&
        strcasecmp(value, "keep-alive") == 0;

    value = strmap_get(request->headers, "transfer-encoding");
    if (value == NULL || strcasecmp(value, "chunked") != 0) {
        /* not chunked */

        value = strmap_get(request->headers, "content-length");
        if (value == NULL) {
            /* no body at all */

            request->body = NULL;
            connection->request.read_state = READ_END;

            return;
        } else {
            char *endptr;

            content_length = strtoul(value, &endptr, 10);
            if (unlikely(*endptr != 0 || content_length < 0)) {
                daemon_log(2, "invalid Content-Length header in HTTP request\n");
                http_server_connection_close(connection);
                return;
            }
        }
    } else {
        /* chunked */

        content_length = (off_t)-1;
    }

    /* istream_deinit() used poison_noaccess() - make it writable now
       for re-use */
    poison_undefined(&connection->request.body_reader,
                     sizeof(connection->request.body_reader));

    request->body = http_body_init(&connection->request.body_reader,
                                   &http_server_request_stream,
                                   connection->pool, request->pool,
                                   content_length, 1);

    connection->request.read_state = READ_BODY;

    value = strmap_get(request->headers, "expect");
    connection->request.expect_100_continue = value != NULL &&
        strcmp(value, "100-continue") == 0;
}

static void
http_server_handle_line(http_server_connection_t connection,
                        const char *line, size_t length)
{
    assert(connection->request.read_state == READ_START ||
           connection->request.read_state == READ_HEADERS);

    if (unlikely(connection->request.read_state == READ_START)) {
        assert(connection->request.request == NULL);

        http_server_parse_request_line(connection, line, length);
    } else if (likely(length > 0)) {
        assert(connection->request.read_state == READ_HEADERS);
        assert(connection->request.request != NULL);

        header_parse_line(connection->request.request->pool,
                          connection->request.request->headers,
                          line, length);
    } else {
        assert(connection->request.read_state == READ_HEADERS);
        assert(connection->request.request != NULL);

        http_server_headers_finished(connection);
    }
}

static int
http_server_parse_headers(http_server_connection_t connection)
{
    const char *buffer, *buffer_end, *start, *end, *next = NULL;
    size_t length;

    assert(connection->request.read_state == READ_START ||
           connection->request.read_state == READ_HEADERS);

    buffer = fifo_buffer_read(connection->input, &length);
    if (buffer == NULL)
        return 0;

    assert(length > 0);
    buffer_end = buffer + length;

    start = buffer;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        next = end + 1;
        --end;
        if (likely(*end == '\r'))
            --end;
        while (unlikely(end >= start && char_is_whitespace(*end)))
            --end;

        http_server_handle_line(connection, start, end - start + 1);
        if (connection->request.read_state != READ_HEADERS)
            break;

        start = next;
    }

    if (next == NULL)
        return 0;

    fifo_buffer_consume(connection->input, next - buffer);
    return 1;
}

static void
http_server_consume_input(http_server_connection_t connection)
{
    if (connection->request.read_state == READ_START ||
        connection->request.read_state == READ_HEADERS) {
        if (http_server_parse_headers(connection) &&
            (connection->request.read_state == READ_BODY ||
             connection->request.read_state == READ_END))
            connection->handler->request(connection->request.request,
                                         connection->handler_ctx,
                                         &connection->request.async_ref);
    } else if (connection->request.read_state == READ_BODY) {
        http_server_consume_body(connection);
    }
}

static void
http_server_try_read_buffered(http_server_connection_t connection)
{
    ssize_t nbytes;

    if (connection->request.read_state == READ_BODY) {
        http_server_maybe_send_100_continue(connection);
        if (!http_server_connection_valid(connection))
            return;
    }

    nbytes = read_to_buffer(connection->fd, connection->input, INT_MAX);
    assert(nbytes != -2);

    if (unlikely(nbytes < 0)) {
        if (errno == EAGAIN) {
            event2_or(&connection->event, EV_READ);
            return;
        }

        daemon_log(1, "read error on HTTP connection: %s\n", strerror(errno));
        http_server_connection_close(connection);
        return;
    }

    if (unlikely(nbytes == 0)) {
        /* XXX */
        http_server_connection_close(connection);
        return;
    }

    http_server_consume_input(connection);

    if (http_server_connection_valid(connection) &&
        (connection->request.read_state == READ_START ||
         connection->request.read_state == READ_HEADERS ||
         connection->request.read_state == READ_BODY) &&
        !fifo_buffer_full(connection->input))
        event2_or(&connection->event, EV_READ);
}

static void
http_server_try_request_direct(http_server_connection_t connection)
{
    ssize_t nbytes;

    assert(connection->fd >= 0);
    assert(connection->request.read_state == READ_BODY);

    http_server_maybe_send_100_continue(connection);
    if (!http_server_connection_valid(connection))
        return;

    nbytes = http_body_try_direct(&connection->request.body_reader, connection->fd);
    if (nbytes < 0) {
        /* XXX EAGAIN? */
        daemon_log(1, "read error on HTTP connection: %s\n", strerror(errno));
        http_server_connection_close(connection);
        return;
    }

    if (nbytes > 0 && http_body_eof(&connection->request.body_reader)) {
        connection->request.read_state = READ_END;
        istream_deinit_eof(&connection->request.body_reader.output);
    }
}

void
http_server_try_read(http_server_connection_t connection)
{
    if (connection->request.read_state == READ_BODY &&
        (connection->request.body_reader.output.handler_direct & ISTREAM_SOCKET) != 0) {
        if (fifo_buffer_empty(connection->input))
            http_server_try_request_direct(connection);
        else
            http_server_consume_body(connection);
    } else
        http_server_try_read_buffered(connection);
}
