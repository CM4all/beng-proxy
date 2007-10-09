/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server.h"
#include "fifo-buffer.h"
#include "strutil.h"
#include "compiler.h"
#include "buffered-io.h"
#include "header-parser.h"
#include "header-writer.h"
#include "event2.h"
#include "date.h"
#include "http-body.h"
#include "direct.h"
#include "format.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>


static const char *http_status_to_string_data[][20] = {
    [2] = {
        [HTTP_STATUS_OK - 200] = "200 OK",
        [HTTP_STATUS_CREATED - 200] = "201 Created",
        [HTTP_STATUS_NO_CONTENT - 200] = "204 No Content",
        [HTTP_STATUS_PARTIAL_CONTENT - 200] = "206 Partial Content",
    },
    [4] = {
        [HTTP_STATUS_BAD_REQUEST - 400] = "400 Bad Request",
        [HTTP_STATUS_UNAUTHORIZED - 400] = "401 Unauthorized",
        [HTTP_STATUS_FORBIDDEN - 400] = "403 Forbidden",
        [HTTP_STATUS_NOT_FOUND - 400] = "404 Not Found",
        [HTTP_STATUS_METHOD_NOT_ALLOWED - 400] = "405 Method Not Allowed",
    },
    [5] = {
        [HTTP_STATUS_INTERNAL_SERVER_ERROR - 500] = "500 Internal Server Error",
        [HTTP_STATUS_NOT_IMPLEMENTED - 500] = "501 Not Implemented",
        [HTTP_STATUS_BAD_GATEWAY - 500] = "502 Bad Gateway",
        [HTTP_STATUS_SERVICE_UNAVAILABLE - 500] = "503 Service Unavailable",
        [HTTP_STATUS_GATEWAY_TIMEOUT - 500] = "504 Gateway Timeout",
        [HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED - 500] = "505 HTTP Version Not Supported",
    },
};

struct http_server_connection {
    pool_t pool;

    /*
    struct sockaddr_storage remote_addr;
    socklen_t remote_addrlen;
    */

    /* I/O */
    int fd;
    struct event2 event;
    fifo_buffer_t input;

    /* handler */
    const struct http_server_connection_handler *handler;
    void *handler_ctx;

    /* request */
    struct {
        enum {
            READ_START,
            READ_HEADERS,
            READ_BODY,
            READ_END
        } read_state;
        struct http_server_request *request;

        struct http_body_reader body_reader;
    } request;

    /* response */
    struct {
        int writing;
        char status_buffer[64];
        char content_length_buffer[32];
        istream_t istream;
    } response;

    /* connection settings */
    int keep_alive;
#ifdef __linux
    int cork;
#endif
};

static struct http_server_request *
http_server_request_new(http_server_connection_t connection)
{
    pool_t pool;
    struct http_server_request *request;

    assert(connection != NULL);

    pool = pool_new_linear(connection->pool, "http_server_request", 16384);
    request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->connection = connection;
    request->headers = strmap_new(pool, 64);

    return request;
}

static void
http_server_request_free(struct http_server_request **request_r)
{
    struct http_server_request *request = *request_r;
    *request_r = NULL;

    if (request->body != NULL)
        istream_free(&request->body);

    pool_unref(request->pool);
}

static inline int
http_server_connection_valid(http_server_connection_t connection)
{
    return connection->fd >= 0;
}

static void
http_server_connection_close(http_server_connection_t connection);

static inline void
http_server_cork(http_server_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);

    if (!connection->cork) {
        connection->cork = 1;
#ifdef __linux
        setsockopt(connection->fd, IPPROTO_TCP, TCP_CORK,
                   &connection->cork, sizeof(connection->cork));
#endif
    }
}

static inline void
http_server_uncork(http_server_connection_t connection)
{
    assert(connection != NULL);

    if (connection->cork) {
        assert(connection->fd >= 0);
        connection->cork = 0;
#ifdef __linux
        setsockopt(connection->fd, IPPROTO_TCP, TCP_CORK,
                   &connection->cork, sizeof(connection->cork));
#endif
    }
}


static void
http_server_consume_body(http_server_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->request.read_state == READ_BODY);

    http_body_consume_body(&connection->request.body_reader, connection->input);

    if (!http_server_connection_valid(connection))
        return;

    event2_setbit(&connection->event, EV_READ, !fifo_buffer_full(connection->input));
}

static void
http_server_try_request_direct(http_server_connection_t connection)
{
    ssize_t nbytes;

    assert(connection->fd >= 0);
    assert(connection->request.read_state == READ_BODY);

    nbytes = http_body_try_direct(&connection->request.body_reader, connection->fd);
    if (nbytes < 0) {
        /* XXX EAGAIN? */
        perror("read error on HTTP connection");
        http_server_connection_close(connection);
        return;
    }
}

static void
http_server_try_read(http_server_connection_t connection);


/*
 * istream implementation for the request body
 *
 */

static inline http_server_connection_t
response_stream_to_connection(istream_t istream)
{
    return (http_server_connection_t)(((char*)istream) - offsetof(struct http_server_connection, request.body_reader.output));
}

static void
http_server_request_stream_read(istream_t istream)
{
    http_server_connection_t connection = response_stream_to_connection(istream);

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->request.read_state == READ_BODY);
    assert(connection->request.body_reader.output.handler != NULL);

    pool_ref(connection->pool);

    http_server_consume_body(connection);

    if (connection->request.read_state == READ_BODY)
        http_server_try_read(connection);

    pool_unref(connection->pool);
}

static void
http_server_request_stream_close(istream_t istream)
{
    http_server_connection_t connection = response_stream_to_connection(istream);

    if (connection->request.read_state == READ_END)
        return;

    assert(connection->request.read_state == READ_BODY);

    event2_nand(&connection->event, EV_READ);

    connection->request.read_state = READ_END;

    if (connection->request.request != NULL)
        connection->request.request->body = NULL;

    if (!http_body_eof(&connection->request.body_reader))
        connection->keep_alive = 0;

    istream_invoke_free(&connection->request.body_reader.output);

    http_body_deinit(&connection->request.body_reader);
}

static const struct istream http_server_request_stream = {
    .read = http_server_request_stream_read,
    .close = http_server_request_stream_close,
};


static void
http_server_try_write(http_server_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->request.read_state != READ_START &&
           connection->request.read_state != READ_HEADERS);
    assert(connection->request.request != NULL);
    assert(connection->response.writing);

    http_server_cork(connection);
    event2_lock(&connection->event);
    istream_read(connection->response.istream);
    event2_unlock(&connection->event);
    http_server_uncork(connection);
}

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

    value = strmap_get(request->headers, "connection");
    connection->keep_alive = value != NULL &&
        strcasecmp(value, "keep-alive") == 0;

    value = strmap_get(request->headers, "transfer-encoding");
    if (value == NULL || strcasecmp(value, "chunked") != 0) {
        /* not chunked */

        value = strmap_get(request->headers, "content-length");
        if (value == NULL) {
            /* no body at all */

            request->content_length = 0;
            request->body = NULL;
            connection->request.read_state = READ_END;
        } else {
            char *endptr;

            request->content_length = strtoul(value, &endptr, 10);
            if (unlikely(*endptr != 0 || request->content_length < 0)) {
                fprintf(stderr, "invalid Content-Length header in HTTP request\n");
                http_server_connection_close(connection);
                return;
            }

            http_body_init(&connection->request.body_reader,
                           &http_server_request_stream, request->pool,
                           request->content_length);

            request->body = http_body_istream(&connection->request.body_reader);
            connection->request.read_state = READ_BODY;
        }
    } else {
        /* chunked */

        request->content_length = (off_t)-1;

        http_body_init(&connection->request.body_reader,
                       &http_server_request_stream, request->pool,
                       request->content_length);

        request->body
            = istream_dechunk_new(request->pool,
                                  http_body_istream(&connection->request.body_reader),
                                  http_body_dechunked_eof, &connection->request.body_reader);

        connection->request.read_state = READ_BODY;
    }

    connection->handler->request(connection->request.request, connection->handler_ctx);

    assert(connection->request.request == NULL ||
           connection->request.request->body == NULL ||
           connection->request.request->body->handler != NULL);
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
    while (1) {
        if (connection->request.read_state == READ_START ||
            connection->request.read_state == READ_HEADERS) {
            if (http_server_parse_headers(connection) == 0)
                break;
        } else if (connection->request.read_state == READ_BODY) {
            http_server_consume_body(connection);
            break;
        } else {
            break;
        }
    }
}

static void
http_server_try_read_buffered(http_server_connection_t connection)
{
    ssize_t nbytes;

    nbytes = read_to_buffer(connection->fd, connection->input, INT_MAX);
    assert(nbytes != -2);

    if (unlikely(nbytes < 0)) {
        if (errno == EAGAIN) {
            event2_or(&connection->event, EV_READ);
            return;
        }

        perror("read error on HTTP connection");
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
http_server_try_read(http_server_connection_t connection)
{
    if (connection->request.read_state == READ_BODY &&
        (connection->request.body_reader.output.handler_direct & ISTREAM_SOCKET) != 0 &&
        fifo_buffer_empty(connection->input))
        http_server_try_request_direct(connection);
    else
        http_server_try_read_buffered(connection);
}

static void
http_server_event_callback(int fd, short event, void *ctx)
{
    http_server_connection_t connection = ctx;

    (void)fd;

    pool_ref(connection->pool);

    event2_reset(&connection->event);
    event2_lock(&connection->event);

    if (unlikely(event & EV_TIMEOUT)) {
        fprintf(stderr, "timeout\n");
        http_server_connection_close(connection);
    }

    if (http_server_connection_valid(connection) && (event & EV_WRITE) != 0)
        http_server_try_write(connection);

    if (http_server_connection_valid(connection) && (event & EV_READ) != 0)
        http_server_try_read(connection);

    if (likely(http_server_connection_valid(connection)))
        event2_unlock(&connection->event);

    pool_unref(connection->pool);
    pool_commit();
}

void
http_server_connection_new(pool_t pool, int fd,
                           const struct http_server_connection_handler *handler,
                           void *ctx,
                           http_server_connection_t *connection_r)
{
    http_server_connection_t connection;
    static const struct timeval tv = {
        .tv_sec = 30,
        .tv_usec = 0,
    };

    assert(fd >= 0);
    assert(handler != NULL);
    assert(handler->request != NULL);

    connection = p_malloc(pool, sizeof(*connection));
    connection->pool = pool;
    connection->fd = fd;
    connection->handler = handler;
    connection->handler_ctx = ctx;
    connection->request.read_state = READ_START;
    connection->request.request = NULL;
    connection->response.writing = 0;
    connection->cork = 0;

    connection->input = fifo_buffer_new(pool, 4096);

    pool_ref(connection->pool);

    event2_init(&connection->event, connection->fd,
                http_server_event_callback, connection,
                &tv);
    event2_lock(&connection->event);

    *connection_r = connection;

    http_server_try_read(connection);

    event2_unlock(&connection->event);
    pool_unref(connection->pool);
}

static void
http_server_connection_close(http_server_connection_t connection)
{
    assert(connection != NULL);

    if (connection->fd >= 0) {
        event2_set(&connection->event, 0);
        close(connection->fd);
        connection->fd = -1;
    }

    connection->cork = 0;

    pool_ref(connection->pool);

    if (connection->request.read_state != READ_START) {
        pool_t pool;

        assert(connection->request.request != NULL);

        pool = connection->request.request->pool;
        assert(pool != NULL);
        pool_ref(pool);

        http_server_request_free(&connection->request.request);

        connection->request.read_state = READ_START;

        if (connection->response.writing) {
            if (connection->response.istream != NULL)
                istream_free(&connection->response.istream);

            connection->response.writing = 0;
        }

        pool_unref(pool);
    }

    if (connection->handler != NULL && connection->handler->free != NULL) {
        const struct http_server_connection_handler *handler = connection->handler;
        void *handler_ctx = connection->handler_ctx;
        connection->handler = NULL;
        connection->handler_ctx = NULL;
        handler->free(handler_ctx);
    }

    pool_unref(connection->pool);
}

void
http_server_connection_free(http_server_connection_t *connection_r)
{
    http_server_connection_t connection = *connection_r;
    *connection_r = NULL;

    assert(connection != NULL);

    http_server_connection_close(connection);
}



/*
 * istream handler for the response
 *
 */

static size_t
http_server_response_stream_data(const void *data, size_t length, void *ctx)
{
    http_server_connection_t connection = ctx;
    ssize_t nbytes;

    assert(connection->fd >= 0);
    assert(connection->response.writing);
    assert(connection->response.istream != NULL);

    nbytes = write(connection->fd, data, length);

    if (likely(nbytes >= 0)) {
        event2_or(&connection->event, EV_WRITE);
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        event2_or(&connection->event, EV_WRITE);
        return 0;
    }

    perror("write error on HTTP connection");
    http_server_connection_close(connection);
    return 0;
}

#ifdef __linux
static ssize_t
http_server_response_stream_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    http_server_connection_t connection = ctx;
    ssize_t nbytes;

    assert(connection->response.writing);

    nbytes = istream_direct_to_socket(type, fd, connection->fd, max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN))
        return -2;

    if (likely(nbytes > 0))
        event2_or(&connection->event, EV_WRITE);

    return nbytes;
}
#endif

static void
http_server_response_stream_eof(void *ctx)
{
    http_server_connection_t connection = ctx;

    assert(connection->request.read_state != READ_START &&
           connection->request.read_state != READ_HEADERS);
    assert(connection->request.request != NULL);
    assert(connection->response.writing);
    assert(connection->response.istream != NULL);
    assert(connection->response.istream->pool != NULL);

    pool_ref(connection->pool);

    istream_handler_clear(connection->response.istream);
    connection->response.istream = NULL;

    if (connection->request.read_state == READ_BODY) {
        /* We are still reading the request body, which we don't need
           anymore.  To discard it, we simply close the connection by
           disabling keepalive; this seems cheaper than redirecting
           the rest of the body to /dev/null */
        connection->keep_alive = 0;
    }

    http_server_request_free(&connection->request.request);

    connection->request.read_state = READ_START;
    connection->response.writing = 0;

    if (connection->keep_alive) {
        /* set up events for next request */
        event2_set(&connection->event, EV_READ);
    } else {
        /* keepalive disabled and response is finished: we must close
           the connection */
        http_server_connection_close(connection);
    }

    pool_unref(connection->pool);
}

static void
http_server_response_stream_free(void *ctx)
{
    http_server_connection_t connection = ctx;

    assert(connection->response.writing);

    http_server_connection_close(connection);
}

static const struct istream_handler http_server_response_stream_handler = {
    .data = http_server_response_stream_data,
#ifdef __linux
    .direct = http_server_response_stream_direct,
#endif
    .eof = http_server_response_stream_eof,
    .free = http_server_response_stream_free,
};


static const char *
http_status_to_string(http_status_t status)
{
    assert((status / 100) < sizeof(http_status_to_string_data) / sizeof(http_status_to_string_data[0]));
    assert(status % 100 < sizeof(http_status_to_string_data[0]) / sizeof(http_status_to_string_data[0][0]));

    return http_status_to_string_data[status / 100][status % 100];
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
                     growing_buffer_t headers,
                     off_t content_length, istream_t body)
{
    http_server_connection_t connection = request->connection;
    istream_t status_stream, header_stream;

    assert(connection->request.request == request);
    assert(!connection->response.writing);

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

    if (content_length == (off_t)-1) {
        if (body != NULL && connection->keep_alive) {
            header_write(headers, "transfer-encoding", "chunked");
            body = istream_chunked_new(request->pool, body);
        }
    } else {
        format_uint64(connection->response.content_length_buffer, content_length);
        header_write(headers, "content-length",
                     connection->response.content_length_buffer);
    }

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

    connection->response.writing = 1;

    pool_ref(connection->pool);

    http_server_try_write(connection);

    pool_unref(connection->pool);
}

void
http_server_send_message(struct http_server_request *request,
                         http_status_t status, const char *msg)
{
    size_t length = strlen(msg);

    http_server_response(request, status, NULL,
                         length,
                         istream_memory_new(request->pool, msg, length));
}
