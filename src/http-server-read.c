/*
 * HTTP server implementation.
 *
 * istream implementation for the request body.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "http-util.h"
#include "strutil.h"
#include "strmap.h"
#include "buffered_io.h"
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
        } else if (line[1] == 'U' && line[2] == 'T' && line[3] == ' ') {
            method = HTTP_METHOD_PUT;
            line += 4;
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

        http_server_error_message(connection, "unrecognized request method");
        return false;
    }

    space = memchr(line, ' ', eol - line);
    if (unlikely(space == NULL || space + 6 > line + length ||
                 memcmp(space + 1, "HTTP/", 5) != 0)) {
        /* refuse HTTP 0.9 requests */
        static const char msg[] =
            "This server requires HTTP 1.1.";

        ssize_t nbytes = filtered_socket_write(&connection->socket,
                                               msg, sizeof(msg) - 1);
        if (nbytes != WRITE_DESTROYED)
            http_server_done(connection);
        return false;
    }

    connection->request.request = http_server_request_new(connection);
    connection->request.request->method = method;
    connection->request.request->uri = p_strndup(connection->request.request->pool, line, space - line);
    connection->request.read_state = READ_HEADERS;
    connection->request.http_1_0 = space + 9 <= line + length &&
        space[8] == '0' && space[7] == '.' && space[6] == '1';

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

    /* disable the idle+headers timeout; the request body timeout will
       be tracked by filtered_socket (auto-refreshing) */
    evtimer_del(&connection->idle_timeout);

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
        (value == NULL || !http_list_contains_i(value, "close"));

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

    /* for the response body, the filtered_socket class tracks
       inactivity timeout */
    filtered_socket_schedule_read_timeout(&connection->socket, false,
                                          &http_server_read_timeout);

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

static enum buffered_result
http_server_feed_headers(struct http_server_connection *connection,
                         const void *_data, size_t length)
{
    assert(connection->request.read_state == READ_START ||
           connection->request.read_state == READ_HEADERS);

    if (connection->request.bytes_received >= 64 * 1024) {
        daemon_log(2, "http_server: too many request headers\n");
        http_server_connection_close(connection);
        return false;
    }

    const char *const buffer = _data, *const buffer_end = buffer + length;
    if (buffer == NULL)
        return true;

    const char *start = buffer, *end, *next = NULL;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        next = end + 1;
        --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        if (!http_server_handle_line(connection, start, end - start + 1))
            return BUFFERED_CLOSED;

        if (connection->request.read_state != READ_HEADERS)
            break;

        start = next;
    }

    size_t consumed = 0;
    if (next != NULL) {
        consumed = next - buffer;
        connection->request.bytes_received += consumed;
        filtered_socket_consumed(&connection->socket, consumed);
    }

    return connection->request.read_state == READ_HEADERS
        ? BUFFERED_MORE
        : (consumed == length ? BUFFERED_OK : BUFFERED_PARTIAL);
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
        filtered_socket_schedule_read_no_timeout(&connection->socket, false);

    pool_ref(connection->pool);

    if (connection->request.expect_failed)
        http_server_send_message(connection->request.request,
                                 HTTP_STATUS_EXPECTATION_FAILED,
                                 "Unrecognized expectation");
    else {
        connection->request.in_handler = true;
        connection->handler->request(connection->request.request,
                                     connection->handler_ctx,
                                     &connection->request.async_ref);
        connection->request.in_handler = false;
    }

    bool ret = http_server_connection_valid(connection);
    pool_unref(connection->pool);

    return ret;
}

enum buffered_result
http_server_feed(struct http_server_connection *connection,
                  const void *data, size_t length)
{
    switch (connection->request.read_state) {
        enum buffered_result result;

    case READ_START:
    case READ_HEADERS:
        if (connection->score == HTTP_SERVER_NEW)
            connection->score = HTTP_SERVER_FIRST;

        result = http_server_feed_headers(connection, data, length);
        if ((result == BUFFERED_OK || result == BUFFERED_PARTIAL) &&
            (connection->request.read_state == READ_BODY ||
             connection->request.read_state == READ_END)) {
            if (connection->request.read_state == READ_BODY)
                result =
                    http_body_require_more(&connection->request.body_reader)
                    ? BUFFERED_AGAIN_EXPECT
                    : BUFFERED_AGAIN_OPTIONAL;

            if (!http_server_submit_request(connection))
                result = BUFFERED_CLOSED;
        }

        return result;

    case READ_BODY:
        return http_server_feed_body(connection, data, length);

    case READ_END:
        /* check if the connection was closed by the client while we
           were processing the request */

        if (filtered_socket_full(&connection->socket))
            /* the buffer is full, the peer has been pipelining too
               much - that would disallow us to detect a disconnect;
               let's disable keep-alive now and discard all data */
            connection->keep_alive = false;

        if (!connection->keep_alive) {
            /* discard all pipelined input when keep-alive has been
               disabled */
            filtered_socket_consumed(&connection->socket, length);
            return BUFFERED_OK;
        }

        return BUFFERED_MORE;
    }

    assert(false);
    gcc_unreachable();
}

enum direct_result
http_server_try_request_direct(struct http_server_connection *connection,
                               int fd, enum istream_direct fd_type)
{
    ssize_t nbytes;

    assert(http_server_connection_valid(connection));
    assert(connection->request.read_state == READ_BODY);

    if (!http_server_maybe_send_100_continue(connection))
        return DIRECT_CLOSED;

    nbytes = http_body_try_direct(&connection->request.body_reader,
                                  fd, fd_type);
    if (nbytes == ISTREAM_RESULT_BLOCKING)
        /* the destination fd blocks */
        return DIRECT_BLOCKING;

    if (nbytes == ISTREAM_RESULT_CLOSED)
        /* the stream (and the whole connection) has been closed
           during the direct() callback (-3); no further checks */
        return DIRECT_CLOSED;

    if (nbytes < 0) {
        if (errno == EAGAIN)
            return DIRECT_EMPTY;

        return DIRECT_ERRNO;
    }

    if (nbytes == ISTREAM_RESULT_EOF)
        return DIRECT_END;

    connection->request.bytes_received += nbytes;

    if (http_body_eof(&connection->request.body_reader)) {
        connection->request.read_state = READ_END;
        istream_deinit_eof(&connection->request.body_reader.output);
        return http_server_connection_valid(connection)
            ? DIRECT_OK
            : DIRECT_CLOSED;
    } else {
        return DIRECT_OK;
    }
}
