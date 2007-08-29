/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-client.h"
#include "fifo-buffer.h"
#include "strutil.h"
#include "compiler.h"
#include "buffered-io.h"
#include "header-parser.h"
#include "header-writer.h"
#include "event2.h"

#ifdef __linux
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct http_client_connection {
    pool_t pool;

    /* I/O */
    int fd;
    struct event2 event;
    fifo_buffer_t input;

    /* handler */
    const struct http_client_connection_handler *handler;
    void *handler_ctx;

    /* request */
    struct {
        pool_t pool;
        istream_t istream;
        int blocking;
        char request_line_buffer[1024];

        const struct http_client_response_handler *handler;
        void *handler_ctx;
    } request;

    /* response */
    struct {
        enum {
            READ_NONE,
            READ_STATUS,
            READ_HEADERS,
            READ_BODY
        } read_state;
        int status;
        strmap_t headers;
        off_t content_length, body_rest;
        struct istream stream;
        int direct_mode;
        istream_t body;
    } response;

    /* connection settings */
    int keep_alive;
#ifdef __linux
    int cork;
#endif
};

static inline http_client_connection_t
response_stream_to_connection(istream_t istream)
{
    return (http_client_connection_t)(((char*)istream) - offsetof(struct http_client_connection, response.stream));
}

static inline int
http_client_connection_valid(http_client_connection_t connection)
{
    return connection->fd >= 0;
}

static void
http_client_consume_body(http_client_connection_t connection);

static void
http_client_response_stream_read(istream_t istream)
{
    http_client_connection_t connection = response_stream_to_connection(istream);
    pool_ref(connection->pool);

    connection->response.direct_mode = 0;

    http_client_consume_body(connection);

    pool_unref(connection->pool);
}

static void
http_client_try_response_direct(http_client_connection_t connection);

static void
http_client_response_stream_direct(istream_t istream)
{
    http_client_connection_t connection = response_stream_to_connection(istream);

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->response.read_state == READ_BODY);
    assert(connection->response.stream.handler != NULL);
    assert(connection->response.stream.handler->direct != NULL);

    connection->response.direct_mode = 1;

    http_client_try_response_direct(connection);
}

static void
http_client_response_stream_close(istream_t istream)
{
    http_client_connection_t connection = response_stream_to_connection(istream);

    if (connection->response.read_state == READ_NONE)
        return;

    assert(connection->response.read_state == READ_BODY);
    assert(connection->request.pool != NULL);
    assert(connection->request.istream == NULL);

    if (connection->request.handler != NULL &&
        connection->request.handler->free != NULL) {
        const struct http_client_response_handler *handler = connection->request.handler;
        void *handler_ctx = connection->request.handler_ctx;
        connection->request.handler = NULL;
        connection->request.handler_ctx = NULL;
        handler->free(handler_ctx);
    }

    connection->response.read_state = READ_NONE;
    connection->response.headers = NULL;
    connection->response.direct_mode = 0;
    connection->response.body = NULL;

    if (connection->response.body_rest > 0) {
        /* XXX invalidate connection */
    }

    istream_invoke_free(istream);

    if (connection->request.pool != NULL) {
        pool_unref(connection->request.pool);
        connection->request.pool = NULL;
    }

    if (!connection->keep_alive) {
        http_client_connection_close(connection);
        return;
    }

    if (http_client_connection_valid(connection) &&
        connection->handler != NULL &&
        connection->handler->idle != NULL) {
        connection->handler->idle(connection->handler_ctx);
    }
}

static const struct istream http_client_response_stream = {
    .read = http_client_response_stream_read,
    .direct = http_client_response_stream_direct,
    .close = http_client_response_stream_close,
};

static void
http_client_response_body_consumed(http_client_connection_t connection, size_t nbytes)
{
    assert(connection->response.read_state == READ_BODY);
    assert(connection->request.pool != NULL);
    assert(connection->request.istream == NULL);

    if (connection->response.body_rest == (off_t)-1)
        return;

    assert((off_t)nbytes <= connection->response.body_rest);

    connection->response.body_rest -= (off_t)nbytes;
    if (connection->response.body_rest > 0)
        return;

    pool_ref(connection->pool);

    istream_invoke_eof(&connection->response.stream);

    http_client_response_stream_close(&connection->response.stream);

    pool_unref(connection->pool);
}

static inline void
http_client_cork(http_client_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);

#ifdef __linux
    if (!connection->cork) {
        connection->cork = 1;
        setsockopt(connection->fd, IPPROTO_TCP, TCP_CORK,
                   &connection->cork, sizeof(connection->cork));
    }
#else
    (void)connection;
#endif
}

static inline void
http_client_uncork(http_client_connection_t connection)
{
    assert(connection != NULL);

#ifdef __linux
    if (connection->cork) {
        assert(connection->fd >= 0);
        connection->cork = 0;
        setsockopt(connection->fd, IPPROTO_TCP, TCP_CORK,
                   &connection->cork, sizeof(connection->cork));
    }
#else
    (void)connection;
#endif
}

static void
http_client_parse_status_line(http_client_connection_t connection,
                              const char *line, size_t length)
{
    const char *space;

    assert(connection != NULL);
    assert(connection->request.pool != NULL);
    assert(connection->request.istream == NULL);
    assert(connection->response.headers == NULL);
    assert(connection->response.read_state == READ_STATUS);

    if (length > 4 && memcmp(line, "HTTP", 4) == 0) {
        space = memchr(line + 4, ' ', length - 4);
        if (space != NULL) {
            length -= space - line + 1;
            line = space + 1;
        }
    }

    if (unlikely(length < 3 || !char_is_digit(line[0]) ||
                 !char_is_digit(line[1]) || !char_is_digit(line[2]))) {
        fprintf(stderr, "no HTTP status found\n");
        http_client_connection_close(connection);
        return;
    }

    connection->response.status = ((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0';
    if (unlikely(connection->response.status < 100 || connection->response.status > 599)) {
        http_client_connection_close(connection);
        return;
    }

    connection->response.read_state = READ_HEADERS;
    connection->response.headers = strmap_new(connection->request.pool, 64);

    /* XXX */
}

static void
http_client_headers_finished(http_client_connection_t connection)
{
    const char *header_connection, *value;
    char *endptr;

    header_connection = strmap_get(connection->response.headers, "connection");
    connection->keep_alive = header_connection != NULL &&
        strcasecmp(header_connection, "keep-alive") == 0;

    connection->response.stream = http_client_response_stream;
    connection->response.stream.pool = connection->request.pool;

    value = strmap_get(connection->response.headers, "transfer-encoding");
    if (value == NULL || strcasecmp(value, "chunked") != 0) {
        /* not chunked */

        value = strmap_get(connection->response.headers, "content-length");
        if (unlikely(value == NULL)) {
            if (connection->keep_alive) {
                fprintf(stderr, "no Content-Length header in HTTP response\n");
                http_client_connection_close(connection);
                return;
            }

            connection->response.content_length = (off_t)-1;
        } else {
            connection->response.content_length = strtoul(value, &endptr, 10);
            if (unlikely(*endptr != 0 || connection->response.content_length < 0)) {
                fprintf(stderr, "invalid Content-Length header in HTTP response\n");
                http_client_connection_close(connection);
                return;
            }
        }

        connection->response.body = &connection->response.stream;
    } else {
        /* chunked */

        connection->response.content_length = (off_t)-1;

        connection->response.body
            = istream_dechunk_new(connection->request.pool,
                                  &connection->response.stream);
    }

    connection->response.body_rest = connection->response.content_length;
    connection->response.read_state = READ_BODY;
}

static void
http_client_handle_line(http_client_connection_t connection,
                        const char *line, size_t length)
{
    assert(connection != NULL);
    assert(connection->response.read_state == READ_STATUS ||
           connection->response.read_state == READ_HEADERS);

    if (connection->response.read_state == READ_STATUS) {
        http_client_parse_status_line(connection, line, length);
    } else if (length > 0) {
        assert(connection->response.read_state == READ_HEADERS);

        header_parse_line(connection->request.pool,
                          connection->response.headers,
                          line, length);
    } else {
        assert(connection->response.read_state == READ_HEADERS);

        http_client_headers_finished(connection);
    }
}

static int
http_client_parse_headers(http_client_connection_t connection)
{
    const char *buffer, *buffer_end, *start, *end, *next = NULL;
    size_t length;

    assert(connection != NULL);
    assert(connection->response.read_state == READ_STATUS ||
           connection->response.read_state == READ_HEADERS);

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

        http_client_handle_line(connection, start, end - start + 1);
        if (connection->response.read_state != READ_HEADERS)
            break;

        start = next;
    }

    if (next == NULL)
        return 0;

    fifo_buffer_consume(connection->input, next - buffer);

    if (http_client_connection_valid(connection) &&
        connection->response.read_state != READ_HEADERS) {
        assert(connection->response.read_state == READ_BODY);

        connection->request.handler->response(connection->response.status,
                                              connection->response.headers,
                                              connection->response.content_length,
                                              connection->response.body,
                                              connection->request.handler_ctx);

        if (connection->response.read_state == READ_BODY) {
            if (unlikely(connection->response.stream.handler == NULL)) {
                fprintf(stderr, "WARNING: no handler for request\n");
                http_client_connection_close(connection);
                return 0;
            }
        }
    }

    return 1;
}

static inline size_t
http_client_response_max_read(http_client_connection_t connection, size_t length)
{
    assert(connection->response.read_state == READ_BODY);

    if (connection->response.body_rest != (off_t)-1 &&
        connection->response.body_rest < (off_t)length)
        return (size_t)connection->response.body_rest;
    else
        return length;
}

static void
http_client_consume_body(http_client_connection_t connection)
{
    const void *buffer;
    size_t length, consumed;

    assert(connection != NULL);
    assert(connection->response.read_state == READ_BODY);

    /* XXX */

    buffer = fifo_buffer_read(connection->input, &length);
    if (buffer == NULL)
        return;

    length = http_client_response_max_read(connection, length);
    consumed = istream_invoke_data(&connection->response.stream,
                                   buffer, length);
    assert(consumed <= length);

    if (!http_client_connection_valid(connection))
        return;

    if (consumed > 0) {
        fifo_buffer_consume(connection->input, consumed);
        http_client_response_body_consumed(connection, consumed);
    }

    event2_setbit(&connection->event, EV_READ, !fifo_buffer_full(connection->input));
}

static void
http_client_consume_headers(http_client_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->response.read_state == READ_STATUS ||
           connection->response.read_state == READ_HEADERS);

    do {
        if (http_client_parse_headers(connection) == 0)
            break;
    } while (connection->response.read_state == READ_STATUS ||
             connection->response.read_state == READ_HEADERS);

    if (connection->response.read_state == READ_BODY)
        http_client_consume_body(connection);
}

static void
http_client_try_response_direct(http_client_connection_t connection)
{
    ssize_t nbytes;

    assert(connection->fd >= 0);
    assert(connection->response.direct_mode);
    assert(connection->response.read_state == READ_BODY);
    assert(connection->response.stream.handler->direct != NULL);

    nbytes = istream_invoke_direct(&connection->response.stream, connection->fd,
                                   http_client_response_max_read(connection, INT_MAX));
    if (nbytes < 0) {
        /* XXX EAGAIN? */
        perror("read error on HTTP connection");
        http_client_connection_close(connection);
        return;
    }

    if (nbytes > 0)
        http_client_response_body_consumed(connection, (size_t)nbytes);
}

static void
http_client_try_read(http_client_connection_t connection)
{
    if (connection->response.direct_mode &&
        fifo_buffer_empty(connection->input)) {
        http_client_try_response_direct(connection);
    } else {
        ssize_t nbytes;

        nbytes = read_to_buffer(connection->fd, connection->input, INT_MAX);
        assert(nbytes != -2);

        if (nbytes == 0) {
            /* XXX */
            http_client_connection_close(connection);
            return;
        }

        if (nbytes < 0) {
            perror("read error on HTTP connection");
            http_client_connection_close(connection);
            return;
        }

        if (connection->response.read_state == READ_BODY)
            http_client_consume_body(connection);
        else
            http_client_consume_headers(connection);

        if (http_client_connection_valid(connection) &&
            connection->response.read_state != READ_NONE &&
            (connection->response.direct_mode ||
             !fifo_buffer_full(connection->input)))
            event2_or(&connection->event, EV_READ);
    }
}

static void
http_client_event_callback(int fd, short event, void *ctx)
{
    http_client_connection_t connection = ctx;

    (void)fd;

    pool_ref(connection->pool);

    event2_reset(&connection->event);
    event2_lock(&connection->event);

    if (unlikely(event & EV_TIMEOUT)) {
        fprintf(stderr, "timeout\n");
        http_client_connection_close(connection);
    }

    if (http_client_connection_valid(connection) && (event & EV_WRITE) != 0)
        istream_read(connection->request.istream);

    if (http_client_connection_valid(connection) && (event & EV_READ) != 0)
        http_client_try_read(connection);

    if (likely(http_client_connection_valid(connection)))
        event2_unlock(&connection->event);

    pool_unref(connection->pool);
    pool_commit();
}

http_client_connection_t
http_client_connection_new(pool_t pool, int fd,
                           const struct http_client_connection_handler *handler,
                           void *ctx)
{
    http_client_connection_t connection;
    static const struct timeval tv = {
        .tv_sec = 30,
        .tv_usec = 0,
    };

    assert(fd >= 0);
    assert(handler != NULL);
    assert(handler->free != NULL);

#ifdef NDEBUG
    pool_ref(pool);
#else
    pool = pool_new_linear(pool, "http_client_connection", 8192);
#endif

    connection = p_malloc(pool, sizeof(*connection));
    connection->pool = pool;
    connection->fd = fd;

    connection->input = fifo_buffer_new(pool, 4096);

    connection->handler = handler;
    connection->handler_ctx = ctx;

    connection->request.pool = NULL;
    connection->request.istream = NULL;
    connection->response.read_state = READ_NONE;

    event2_init(&connection->event, connection->fd,
                http_client_event_callback, connection,
                &tv);

    return connection;
}

void
http_client_connection_close(http_client_connection_t connection)
{
    assert(connection != NULL);

    pool_ref(connection->pool);

    if (connection->fd >= 0) {
        event2_set(&connection->event, 0);
        close(connection->fd);
        connection->fd = -1;
        pool_unref(connection->pool);
    }

    connection->cork = 0;

    if (connection->request.istream != NULL)
        istream_free(&connection->request.istream);

    if (connection->response.read_state == READ_BODY) {
        http_client_response_stream_close(&connection->response.stream);
        assert(connection->response.read_state == READ_NONE);
    }

    if (connection->request.pool != NULL) {
        pool_unref(connection->request.pool);
        connection->request.pool = NULL;
    }

    if (connection->handler != NULL &&
        connection->handler->free != NULL) {
        const struct http_client_connection_handler *handler = connection->handler;
        void *handler_ctx = connection->handler_ctx;
        connection->handler = NULL;
        connection->handler_ctx = NULL;
        handler->free(handler_ctx);
    }

    pool_unref(connection->pool);
}


static size_t
http_client_request_stream_data(const void *data, size_t length, void *ctx)
{
    http_client_connection_t connection = ctx;
    ssize_t nbytes;

    assert(connection->fd >= 0);
    assert(connection->request.istream != NULL);

    nbytes = write(connection->fd, data, length);
    connection->request.blocking = nbytes < (ssize_t)length;
    if (likely(nbytes >= 0)) {
        event2_or(&connection->event, EV_WRITE);
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        event2_or(&connection->event, EV_WRITE);
        return 0;
    }

    perror("write error on HTTP client connection");
    http_client_connection_close(connection);
    return 0;
}

static void
http_client_request_stream_eof(void *ctx)
{
    http_client_connection_t connection = ctx;

    assert(connection->request.istream != NULL);

    connection->request.istream = NULL;

    connection->response.read_state = READ_STATUS;
    connection->response.headers = NULL;
    connection->response.direct_mode = 0;

    event2_set(&connection->event, EV_READ);
}

static void
http_client_request_stream_free(void *ctx)
{
    http_client_connection_t connection = ctx;

    if (connection->request.istream != NULL)
        http_client_connection_close(connection);
}

static const struct istream_handler http_client_request_stream_handler = {
    .data = http_client_request_stream_data,
    .eof = http_client_request_stream_eof,
    .free = http_client_request_stream_free,
};


void
http_client_request(http_client_connection_t connection,
                    http_method_t method, const char *uri,
                    growing_buffer_t headers,
                    const struct http_client_response_handler *handler,
                    void *ctx)
{
    istream_t request_line_stream, header_stream;

    assert(connection != NULL);
    assert(connection->request.pool == NULL);
    assert(connection->request.istream == NULL);
    assert(connection->response.read_state == READ_NONE);
    assert(handler != NULL);
    assert(handler->response != NULL);

    connection->request.pool = pool_new_linear(connection->pool, "http_client_request", 8192);
    connection->request.handler = handler;
    connection->request.handler_ctx = ctx;

    /* request line */

    (void)method; /* XXX */
    snprintf(connection->request.request_line_buffer,
             sizeof(connection->request.request_line_buffer),
             "GET %s HTTP/1.1\r\nHost: localhost\r\n", uri);

    request_line_stream
        = istream_string_new(connection->request.pool,
                             connection->request.request_line_buffer);

    /* headers */

    if (headers == NULL)
        headers = growing_buffer_new(connection->request.pool, 256);

    /* XXX what if this header already exists? */
    header_write(headers, "user-agent", "beng-proxy v" VERSION);

    growing_buffer_write_buffer(headers, "\r\n", 2);

    header_stream = growing_buffer_istream(headers);

    /* request istream */

    connection->request.istream = istream_cat_new(connection->request.pool,
                                                  request_line_stream,
                                                  header_stream, /* XXX body, */ NULL);
    connection->request.istream->handler = &http_client_request_stream_handler;
    connection->request.istream->handler_ctx = connection;

    pool_ref(connection->pool);

    event2_lock(&connection->event);

    istream_read(connection->request.istream);

    if (likely(http_client_connection_valid(connection)))
        event2_unlock(&connection->event);

    pool_unref(connection->pool);
}
