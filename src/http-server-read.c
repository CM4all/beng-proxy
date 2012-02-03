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

#include <inline/poison.h>
#include <daemon/log.h>

#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>

/**
 * @return false if the connection has been closed
 */
static bool
http_server_parse_request_line(struct http_server_connection *connection,
                               const char *line, size_t length)
{
    const char *eol, *space;
    http_method_t method = HTTP_METHOD_NULL;

    assert(connection != NULL);
    assert(connection->request.read_state == READ_START);
    assert(connection->request.request == NULL);

    if (unlikely(length < 5)) {
        http_server_error_message(connection, "malformed request line");
        return false;
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

        http_server_error_message(connection, "unrecognized request method");
        return false;
    }

    space = memchr(line, ' ', eol - line);
    if (unlikely(space == NULL || space + 6 > line + length ||
                 memcmp(space + 1, "HTTP/", 5) != 0)) {
        /* refuse HTTP 0.9 requests */
        static const char msg[] =
            "This server requires HTTP 1.1.";

        socket_wrapper_write(&connection->socket, msg, sizeof(msg) - 1);
        http_server_done(connection);
        return false;
    }

    connection->request.request = http_server_request_new(connection);
    connection->request.request->method = method;
    connection->request.request->uri = p_strndup(connection->request.request->pool, line, space - line);
    connection->request.read_state = READ_HEADERS;
    connection->request.http_1_0 = space + 9 <= line + length &&
        space[8] == '0' && space[7] == '.' && space[6] == '1';

    /* install the header timeout event when we start reading the
       headers */
    evtimer_add(&connection->timeout, &http_server_header_timeout);

    return true;
}

/**
 * @return false if the connection has been closed
 */
static bool
http_server_headers_finished(struct http_server_connection *connection)
{
    struct http_server_request *request = connection->request.request;
    const char *value;
    off_t content_length;
    bool chunked;

    evtimer_del(&connection->timeout);

    value = strmap_get(request->headers, "expect");
    connection->response.writing_100_continue = false;
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

            return true;
        } else {
            char *endptr;

            content_length = strtoul(value, &endptr, 10);
            if (unlikely(*endptr != 0 || content_length < 0)) {
                http_server_error_message(connection,
                                          "invalid Content-Length header in HTTP request");
                return false;
            }

            if (content_length == 0) {
                /* empty body */

                request->body = istream_null_new(request->pool);
                connection->request.read_state = READ_END;

                return true;
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
    return true;
}

/**
 * @return false if the connection has been closed
 */
static bool
http_server_handle_line(struct http_server_connection *connection,
                        const char *line, size_t length)
{
    assert(connection->request.read_state == READ_START ||
           connection->request.read_state == READ_HEADERS);

    if (unlikely(connection->request.read_state == READ_START)) {
        assert(connection->request.request == NULL);

        return http_server_parse_request_line(connection, line, length);
    } else if (likely(length > 0)) {
        assert(connection->request.read_state == READ_HEADERS);
        assert(connection->request.request != NULL);

        header_parse_line(connection->request.request->pool,
                          connection->request.request->headers,
                          line, length);
        return true;
    } else {
        assert(connection->request.read_state == READ_HEADERS);
        assert(connection->request.request != NULL);

        return http_server_headers_finished(connection);
    }
}

/**
 * @return false if the connection has been closed
 */
static bool
http_server_parse_headers(struct http_server_connection *connection)
{
    const char *buffer, *buffer_end, *start, *end, *next = NULL;
    size_t length;

    assert(connection->request.read_state == READ_START ||
           connection->request.read_state == READ_HEADERS);

    if (connection->request.bytes_received >= 64 * 1024) {
        daemon_log(2, "http_server: too many request headers\n");
        http_server_connection_close(connection);
        return false;
    }

    buffer = fifo_buffer_read(connection->input, &length);
    if (buffer == NULL)
        return true;

    assert(length > 0);
    buffer_end = buffer + length;

    start = buffer;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        next = end + 1;
        --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        if (!http_server_handle_line(connection, start, end - start + 1))
            return false;

        if (connection->request.read_state != READ_HEADERS)
            break;

        start = next;
    }

    if (next == NULL) {
        if (fifo_buffer_full(connection->input)) {
            /* the line is too large for our input buffer */
            http_server_error_message(connection, "request header too long");
            return false;
        }

        return true;
    }

    fifo_buffer_consume(connection->input, next - buffer);
    return true;
}

/**
 * @return false if the connection has been closed
 */
static bool
http_server_submit_request(struct http_server_connection *connection)
{
    if (connection->request.read_state == READ_END)
        /* re-enable the event, to detect client disconnect while
           we're processing the request */
        http_server_schedule_read(connection);

    pool_ref(connection->pool);

    if (connection->request.expect_failed)
        http_server_send_message(connection->request.request,
                                 HTTP_STATUS_EXPECTATION_FAILED,
                                 "Unrecognized expectation");
    else
        connection->handler->request(connection->request.request,
                                     connection->handler_ctx,
                                     &connection->request.async_ref);

    bool ret = http_server_connection_valid(connection);
    pool_unref(connection->pool);

    return ret;
}

bool
http_server_consume_input(struct http_server_connection *connection)
{
    if (connection->request.read_state == READ_START ||
        connection->request.read_state == READ_HEADERS) {
        if (!http_server_parse_headers(connection))
            return false;

        if ((connection->request.read_state == READ_BODY ||
             connection->request.read_state == READ_END) &&
            !http_server_submit_request(connection))
            return false;
    } else if (connection->request.read_state == READ_BODY) {
        if (!http_server_consume_body(connection))
            return false;
    }

    assert(http_server_connection_valid(connection));

    if ((connection->request.read_state == READ_START ||
         connection->request.read_state == READ_HEADERS ||
         connection->request.read_state == READ_BODY) &&
        !fifo_buffer_full(connection->input))
        http_server_schedule_read(connection);

    return true;
}

bool
http_server_read_to_buffer(struct http_server_connection *connection)
{
    assert(!fifo_buffer_full(connection->input));

    ssize_t nbytes = socket_wrapper_read_to_buffer(&connection->socket,
                                                   connection->input,
                                                   INT_MAX);
    if (nbytes > 0) {
        connection->request.bytes_received += nbytes;
        return true;
    } else if (nbytes == 0) {
        /* the client closed the connection; do the same on our side */
        http_server_cancel(connection);
        return false;
    } else if (nbytes == -2) {
        assert(false);
        return true;
    } else if (errno == EAGAIN) {
        http_server_schedule_read(connection);
        return true;
    } else {
        http_server_errno(connection, "read error on HTTP connection");
        return false;
    }
}

/**
 * @return true if data is available in the buffer
 */
static bool
http_server_fill_buffer(struct http_server_connection *connection)
{
    return fifo_buffer_full(connection->input) ||
        (http_server_read_to_buffer(connection) &&
         !fifo_buffer_empty(connection->input));
}

static void
http_server_try_read_buffered(struct http_server_connection *connection)
{
    if (connection->request.read_state == READ_BODY &&
        !http_server_maybe_send_100_continue(connection))
        return;

    if (!http_server_fill_buffer(connection))
        return;

    if (connection->score == HTTP_SERVER_NEW)
        connection->score = HTTP_SERVER_FIRST;

    http_server_consume_input(connection);
}

/**
 * Attempt a "direct" transfer of the request body.  Caller must hold
 * an additional pool reference.
 *
 * @return false if the connection has been closed
 */
static bool
http_server_try_request_direct(struct http_server_connection *connection)
{
    ssize_t nbytes;

    assert(http_server_connection_valid(connection));
    assert(connection->request.read_state == READ_BODY);

    if (!http_server_maybe_send_100_continue(connection))
        return false;

    nbytes = http_body_try_direct(&connection->request.body_reader,
                                  connection->socket.fd,
                                  connection->socket.fd_type);
    if (nbytes == ISTREAM_RESULT_BLOCKING)
        /* the destination fd blocks */
        return true;

    if (nbytes == ISTREAM_RESULT_CLOSED)
        /* the stream (and the whole connection) has been closed
           during the direct() callback (-3); no further checks */
        return false;

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            http_server_schedule_read(connection);
            return true;
        }

        http_server_errno(connection, "read error on HTTP connection");
        return false;
    }

    if (nbytes == ISTREAM_RESULT_EOF)
        return true;

    connection->request.bytes_received += nbytes;

    if (http_body_eof(&connection->request.body_reader)) {
        connection->request.read_state = READ_END;
        istream_deinit_eof(&connection->request.body_reader.output);
        return http_server_connection_valid(connection);
    } else {
        http_server_schedule_read(connection);
        return true;
    }
}

static bool
http_server_try_read2(struct http_server_connection *connection)
{
    if (connection->request.read_state == READ_BODY &&
        istream_check_direct(&connection->request.body_reader.output,
                             connection->socket.fd_type)) {
        if (fifo_buffer_empty(connection->input))
            return http_server_try_request_direct(connection);
        else {
            pool_ref(connection->pool);
            bool valid = http_server_consume_body(connection) ||
                http_server_connection_valid(connection);
            pool_unref(connection->pool);
            return valid;
        }
    } else {
        pool_ref(connection->pool);
        http_server_try_read_buffered(connection);
        bool valid = http_server_connection_valid(connection);
        pool_unref(connection->pool);
        return valid;
    }
}

bool
http_server_try_read(struct http_server_connection *connection)
{
    connection->request.want_read = false;

    if (!http_server_try_read2(connection))
        return false;

    if (!connection->request.want_read) {
        /* the event must always be enabled while reading headers */
        assert(connection->request.read_state != READ_HEADERS);

        event_del(&connection->timeout);
        socket_wrapper_unschedule_read(&connection->socket);
    }

    return true;
}
