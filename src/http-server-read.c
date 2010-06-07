/*
 * HTTP server implementation.
 *
 * istream implementation for the request body.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "strutil.h"
#include "strmap.h"
#include "buffered-io.h"
#include "header-parser.h"
#include "istream-internal.h"

#include <daemon/log.h>

#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>

static void
http_server_parse_request_line(struct http_server_connection *connection,
                               const char *line, size_t length)
{
    const char *eol, *space;
    http_method_t method = HTTP_METHOD_NULL;
    static const struct timeval tv = {
        .tv_sec = 20,
        .tv_usec = 0,
    };

    assert(connection != NULL);
    assert(connection->request.read_state == READ_START);
    assert(connection->request.request == NULL);

    if (unlikely(length < 5)) {
        http_server_connection_close(connection);
        return;
    }

    eol = line + length;

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

    if (method == HTTP_METHOD_NULL) {
        /* invalid request method */

        http_server_connection_close(connection);
        return;
    }

    space = memchr(line, ' ', eol - line);
    if (unlikely(space == NULL || space + 6 > line + length ||
                 memcmp(space + 1, "HTTP/", 5) != 0)) {
        /* refuse HTTP 0.9 requests */
        static const char msg[] =
            "This server requires HTTP 1.1.";

        send(connection->fd, msg, sizeof(msg) - 1, MSG_DONTWAIT|MSG_NOSIGNAL);
        http_server_connection_close(connection);
        return;
    }

    connection->request.request = http_server_request_new(connection);
    connection->request.request->method = method;
    connection->request.request->uri = p_strndup(connection->request.request->pool, line, space - line);
    connection->request.read_state = READ_HEADERS;
    connection->request.http_1_0 = space + 9 <= line + length &&
        space[8] == '0' && space[7] == '.' && space[6] == '1';

    /* install the header timeout event when we start reading the
       headers */
    evtimer_add(&connection->timeout, &tv);
}

static void
http_server_headers_finished(struct http_server_connection *connection)
{
    struct http_server_request *request = connection->request.request;
    const char *value;
    off_t content_length;
    bool chunked;

    evtimer_del(&connection->timeout);

    value = strmap_get(request->headers, "expect");
    connection->request.expect_100_continue = value != NULL &&
        strcmp(value, "100-continue") == 0;
    connection->request.expect_failed = value != NULL &&
        strcmp(value, "100-continue") != 0;

    value = strmap_get(request->headers, "connection");

    /* we disable keep-alive support on ancient HTTP 1.0, because that
       feature was not well-defined and led to problems with some
       clients */
    connection->keep_alive = !connection->request.http_1_0 &&
        (value == NULL || strcasecmp(value, "keep-alive") == 0);

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

            if (content_length == 0) {
                /* empty body */

                request->body = istream_null_new(request->pool);
                connection->request.read_state = READ_END;

                return;
            }
        }

        chunked = false;
    } else {
        /* chunked */

        content_length = (off_t)-1;
        chunked = true;
    }

    /* istream_deinit() used poison_noaccess() - make it writable now
       for re-use */
    poison_undefined(&connection->request.body_reader,
                     sizeof(connection->request.body_reader));

    request->body = http_body_init(&connection->request.body_reader,
                                   &http_server_request_stream,
                                   connection->pool, request->pool,
                                   content_length, chunked);

    connection->request.read_state = READ_BODY;
}

static void
http_server_handle_line(struct http_server_connection *connection,
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

static bool
http_server_parse_headers(struct http_server_connection *connection)
{
    const char *buffer, *buffer_end, *start, *end, *next = NULL;
    size_t length;

    assert(connection->request.read_state == READ_START ||
           connection->request.read_state == READ_HEADERS);

    buffer = fifo_buffer_read(connection->input, &length);
    if (buffer == NULL)
        return false;

    assert(length > 0);
    buffer_end = buffer + length;

    start = buffer;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        next = end + 1;
        --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        http_server_handle_line(connection, start, end - start + 1);
        if (connection->request.read_state != READ_HEADERS)
            break;

        start = next;
    }

    if (next == NULL) {
        if (fifo_buffer_full(connection->input)) {
            /* the line is too large for our input buffer */
            daemon_log(2, "http_server: request header too long\n");
            http_server_connection_close(connection);
        }

        return false;
    }

    fifo_buffer_consume(connection->input, next - buffer);
    return true;
}

static void
http_server_submit_request(struct http_server_connection *connection)
{
    if (connection->request.expect_failed) {
        http_server_send_message(connection->request.request,
                                 HTTP_STATUS_EXPECTATION_FAILED,
                                 "Unrecognized expectation");
        return;
    }

    connection->handler->request(connection->request.request,
                                 connection->handler_ctx,
                                 &connection->request.async_ref);
}

void
http_server_consume_input(struct http_server_connection *connection)
{
    if (connection->request.read_state == READ_START ||
        connection->request.read_state == READ_HEADERS) {
        if (http_server_parse_headers(connection) &&
            (connection->request.read_state == READ_BODY ||
             connection->request.read_state == READ_END))
            http_server_submit_request(connection);
    } else if (connection->request.read_state == READ_BODY) {
        http_server_consume_body(connection);
    }

    if (http_server_connection_valid(connection) &&
        (connection->request.read_state == READ_START ||
         connection->request.read_state == READ_HEADERS ||
         connection->request.read_state == READ_BODY) &&
        !fifo_buffer_full(connection->input))
        event2_or(&connection->event, EV_READ);
}

static void
http_server_try_read_buffered(struct http_server_connection *connection)
{
    ssize_t nbytes;

    if (connection->request.read_state == READ_BODY) {
        http_server_maybe_send_100_continue(connection);
        if (!http_server_connection_valid(connection))
            return;
    }

    nbytes = recv_to_buffer(connection->fd, connection->input, INT_MAX);

    if (unlikely(nbytes < 0 && nbytes != -2)) {
        if (errno == EAGAIN) {
            event2_or(&connection->event, EV_READ);
            return;
        }

        daemon_log(1, "read error on HTTP connection: %s\n", strerror(errno));
        http_server_connection_close(connection);
        return;
    }

    if (unlikely(nbytes == 0)) {
        /* the client closed the connection; do the same on our side */
        http_server_connection_close(connection);
        return;
    }

    if (connection->score == HTTP_SERVER_NEW)
        connection->score = HTTP_SERVER_FIRST;

    http_server_consume_input(connection);
}

static void
http_server_try_request_direct(struct http_server_connection *connection)
{
    ssize_t nbytes;

    assert(connection->fd >= 0);
    assert(connection->request.read_state == READ_BODY);

    http_server_maybe_send_100_continue(connection);
    if (!http_server_connection_valid(connection))
        return;

    nbytes = http_body_try_direct(&connection->request.body_reader,
                                  connection->fd, connection->fd_type);
    if (nbytes == -2 || nbytes == -3)
        /* either the destination fd blocks (-2) or the stream (and
           the whole connection) has been closed during the direct()
           callback (-3); no further checks */
        return;

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            event2_or(&connection->event, EV_READ);
            return;
        }

        daemon_log(1, "read error on HTTP connection: %s\n", strerror(errno));
        http_server_connection_close(connection);
        return;
    }

    if (nbytes == 0)
        return;

    if (http_body_eof(&connection->request.body_reader)) {
        connection->request.read_state = READ_END;
        istream_deinit_eof(&connection->request.body_reader.output);
    } else
        event2_or(&connection->event, EV_READ);
}

void
http_server_try_read(struct http_server_connection *connection)
{
    event2_nand(&connection->event, EV_READ);

    if (connection->request.read_state == READ_BODY &&
        istream_check_direct(&connection->request.body_reader.output, connection->fd_type)) {
        if (fifo_buffer_empty(connection->input))
            http_server_try_request_direct(connection);
        else
            http_server_consume_body(connection);
    } else
        http_server_try_read_buffered(connection);
}
