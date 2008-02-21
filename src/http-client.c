/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-client.h"
#include "http-response.h"
#include "fifo-buffer.h"
#include "strutil.h"
#include "compiler.h"
#include "buffered-io.h"
#include "header-parser.h"
#include "header-writer.h"
#include "event2.h"
#include "http-body.h"

#include <daemon/log.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
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
        char request_line_buffer[1024];
        char content_length_buffer[32];

        struct http_response_handler_ref handler;
    } request;

    /* response */
    struct {
        enum {
            READ_NONE,
            READ_STATUS,
            READ_HEADERS,
            READ_BODY
        } read_state;
        http_status_t status;
        strmap_t headers;
        istream_t body;
        struct http_body_reader body_reader;
    } response;

    /* connection settings */
    int keep_alive;
#ifdef __linux
    int cork;
#endif
};

static inline int
http_client_connection_valid(http_client_connection_t connection)
{
    return connection->fd >= 0;
}

static void
http_client_consume_body(http_client_connection_t connection);

static void
http_client_try_read(http_client_connection_t connection);


/*
 * istream implementation for the response body
 *
 */

static inline http_client_connection_t
response_stream_to_connection(istream_t istream)
{
    return (http_client_connection_t)(((char*)istream) - offsetof(struct http_client_connection, response.body_reader.output));
}

static off_t
http_client_response_stream_available(istream_t istream, int partial attr_unused)
{
    http_client_connection_t connection = response_stream_to_connection(istream);

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->response.read_state == READ_BODY);
    assert(!http_response_handler_defined(&connection->request.handler));

    return http_body_available(&connection->response.body_reader);
}

static void
http_client_response_stream_read(istream_t istream)
{
    http_client_connection_t connection = response_stream_to_connection(istream);

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->response.read_state == READ_BODY);
    assert(connection->response.body_reader.output.handler != NULL);
    assert(!http_response_handler_defined(&connection->request.handler));

    pool_ref(connection->pool);

    http_client_consume_body(connection);

    assert(!fifo_buffer_full(connection->input));

    if (connection->response.read_state == READ_BODY)
        http_client_try_read(connection);

    pool_unref(connection->pool);
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
    assert(!http_response_handler_defined(&connection->request.handler));
    assert(!http_body_eof(&connection->response.body_reader));

    event2_nand(&connection->event, EV_READ);

    connection->response.read_state = READ_NONE;
    connection->response.headers = NULL;
    connection->response.body = NULL;

    istream_invoke_abort(&connection->response.body_reader.output);

    connection->keep_alive = 0;

    http_body_deinit(&connection->response.body_reader);

    if (connection->request.pool != NULL) {
        pool_unref(connection->request.pool);
        connection->request.pool = NULL;
    }

#ifdef VALGRIND
    VALGRIND_MAKE_MEM_UNDEFINED(&connection->request, sizeof(connection->request));
    VALGRIND_MAKE_MEM_UNDEFINED(&connection->response, sizeof(connection->response));
    connection->request.pool = NULL;
    connection->request.istream = NULL;
    connection->response.read_state = READ_NONE;
#endif

    if (!connection->keep_alive && http_client_connection_valid(connection)) {
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
    .available = http_client_response_stream_available,
    .read = http_client_response_stream_read,
    .close = http_client_response_stream_close,
};


/*
static inline void
http_client_cork(http_client_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);

#ifdef __linux
    if (!connection->cork) {
        connection->cork = 1;
        socket_set_cork(connection->fd, connection->cork);
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
        socket_set_cork(connection->fd, connection->cork);
    }
#else
    (void)connection;
#endif
}
*/

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
        daemon_log(2, "no HTTP status found\n");
        http_client_connection_close(connection);
        return;
    }

    connection->response.status = (http_status_t)(((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0');
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
    off_t content_length;

    header_connection = strmap_get(connection->response.headers, "connection");
    connection->keep_alive = header_connection != NULL &&
        strcasecmp(header_connection, "keep-alive") == 0;

    if (http_status_is_empty(connection->response.status)) {
        connection->response.body = NULL;
        connection->response.read_state = READ_BODY;
        return;
    }

    value = strmap_get(connection->response.headers, "transfer-encoding");
    if (value == NULL || strcasecmp(value, "chunked") != 0) {
        /* not chunked */

        value = strmap_get(connection->response.headers, "content-length");
        if (unlikely(value == NULL)) {
            if (connection->keep_alive) {
                daemon_log(2, "no Content-Length header in HTTP response\n");
                http_client_connection_close(connection);
                return;
            }
            content_length = (off_t)-1;
        } else {
            content_length = strtoul(value, &endptr, 10);
            if (unlikely(*endptr != 0 || content_length < 0)) {
                daemon_log(2, "invalid Content-Length header in HTTP response\n");
                http_client_connection_close(connection);
                return;
            }
        }
    } else {
        /* chunked */

        content_length = (off_t)-1;
    }

    connection->response.body
        = http_body_init(&connection->response.body_reader,
                         &http_client_response_stream,
                         connection->pool,
                         connection->request.pool,
                         content_length,
                         connection->keep_alive);

    connection->response.read_state = READ_BODY;
}

static void
http_client_handle_line(http_client_connection_t connection,
                        const char *line, size_t length)
{
    assert(connection != NULL);
    assert(connection->request.pool != NULL);
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

static void
http_client_response_finished(http_client_connection_t connection);

static int
http_client_parse_headers(http_client_connection_t connection)
{
    const char *buffer, *buffer_end, *start, *end, *next = NULL;
    size_t length;

    assert(connection != NULL);
    assert(connection->request.pool != NULL);
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
        int empty_response = connection->response.body == NULL;

        assert(connection->response.read_state == READ_BODY);

        http_response_handler_invoke_response(&connection->request.handler,
                                              connection->response.status,
                                              connection->response.headers,
                                              connection->response.body);

        if (empty_response && http_client_connection_valid(connection))
            http_client_response_finished(connection);
    }

    return 1;
}

static void
http_client_response_finished(http_client_connection_t connection)
{
    assert(connection->response.read_state == READ_BODY);
    assert(connection->request.pool != NULL);
    assert(connection->request.istream == NULL);
    assert(!http_response_handler_defined(&connection->request.handler));

    event2_nand(&connection->event, EV_READ);

    connection->response.read_state = READ_NONE;
    connection->response.headers = NULL;
    connection->response.body = NULL;

    if (connection->request.pool != NULL) {
        pool_unref(connection->request.pool);
        connection->request.pool = NULL;
    }

#ifdef VALGRIND
    VALGRIND_MAKE_MEM_UNDEFINED(&connection->request, sizeof(connection->request));
    VALGRIND_MAKE_MEM_UNDEFINED(&connection->response, sizeof(connection->response));
    connection->request.pool = NULL;
    connection->request.istream = NULL;
    connection->response.read_state = READ_NONE;
#endif

    if (!connection->keep_alive && http_client_connection_valid(connection)) {
        http_client_connection_close(connection);
        return;
    }

    if (http_client_connection_valid(connection) &&
        connection->handler != NULL &&
        connection->handler->idle != NULL) {
        connection->handler->idle(connection->handler_ctx);
    }
}

static void
http_client_response_stream_eof(http_client_connection_t connection)
{
    assert(connection->response.read_state == READ_BODY);
    assert(connection->request.pool != NULL);
    assert(connection->request.istream == NULL);
    assert(!http_response_handler_defined(&connection->request.handler));
    assert(http_body_eof(&connection->response.body_reader));

    http_body_deinit(&connection->response.body_reader);

    http_client_response_finished(connection);
}

static void
http_client_consume_body(http_client_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->response.read_state == READ_BODY);

    http_body_consume_body(&connection->response.body_reader, connection->input);

    if (!http_client_connection_valid(connection))
        return;

    if (http_body_eof(&connection->response.body_reader)) {
        http_client_response_stream_eof(connection);
        if (!http_client_connection_valid(connection))
            return;
    }

    event2_setbit(&connection->event, EV_READ, !fifo_buffer_full(connection->input));
}

static void
http_client_consume_headers(http_client_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->request.pool != NULL);
    assert(connection->response.read_state == READ_STATUS ||
           connection->response.read_state == READ_HEADERS);

    do {
        if (http_client_parse_headers(connection) == 0)
            break;

        if (!http_client_connection_valid(connection))
            return;
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
    assert(connection->response.read_state == READ_BODY);

    nbytes = http_body_try_direct(&connection->response.body_reader, connection->fd);
    if (nbytes < 0) {
        /* XXX EAGAIN? */
        daemon_log(1, "read error on HTTP connection: %s\n", strerror(errno));
        http_client_connection_close(connection);
        return;
    }
}

static void
http_client_try_read_buffered(http_client_connection_t connection)
{
    ssize_t nbytes;

    nbytes = read_to_buffer(connection->fd, connection->input, INT_MAX);
    assert(nbytes != -2);

    if (nbytes == 0) {
        if (connection->response.read_state == READ_BODY) {
            /* XXX what if there is still data in connection->input? */
            http_body_socket_eof(&connection->response.body_reader,
                                 connection->input);
            if (!http_client_connection_valid(connection))
                return;

            http_body_deinit(&connection->response.body_reader);
        }

        connection->response.read_state = READ_NONE;
        http_client_connection_close(connection);
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            event2_or(&connection->event, EV_READ);
            return;
        }

        daemon_log(1, "read error on HTTP connection: %s\n", strerror(errno));
        http_client_connection_close(connection);
        return;
    }

    if (connection->response.read_state == READ_BODY)
        http_client_consume_body(connection);
    else
        http_client_consume_headers(connection);

    if (http_client_connection_valid(connection) &&
        connection->response.read_state != READ_NONE &&
        ((connection->response.body_reader.output.handler_direct & ISTREAM_SOCKET) != 0 ||
         !fifo_buffer_full(connection->input)))
        event2_or(&connection->event, EV_READ);
}

static void
http_client_try_read(http_client_connection_t connection)
{
    if (connection->response.read_state == READ_BODY &&
        (connection->response.body_reader.output.handler_direct & ISTREAM_SOCKET) != 0 &&
        fifo_buffer_empty(connection->input))
        /* XXX ensure connection->input is empty */
        http_client_try_response_direct(connection);
    else
        http_client_try_read_buffered(connection);
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
        daemon_log(4, "timeout\n");
        http_client_connection_close(connection);
    }

    if (http_client_connection_valid(connection) && (event & EV_WRITE) != 0)
        istream_read(connection->request.istream);

    if (http_client_connection_valid(connection) && (event & EV_READ) != 0)
        http_client_try_read(connection);

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

#ifdef __linux
    connection->cork = 0;
#endif

    if (connection->request.istream != NULL)
        istream_free(&connection->request.istream);

    if (connection->response.read_state == READ_BODY) {
        http_client_response_stream_close(http_body_istream(&connection->response.body_reader));
        assert(connection->response.read_state == READ_NONE);
    } else if (connection->request.pool != NULL &&
               http_response_handler_defined(&connection->request.handler) /* XXX eliminate this check */) {
        /* we're not reading the response yet, but we nonetheless want
           to notify the caller (callback) that the response object is
           being freed */
        http_response_handler_invoke_abort(&connection->request.handler);
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

void
http_client_connection_free(http_client_connection_t *connection_r)
{
    http_client_connection_t connection = *connection_r;
    *connection_r = NULL;

    assert(connection != NULL);

    http_client_connection_close(connection);
}


/*
 * istream handler for the request
 *
 */

static size_t
http_client_request_stream_data(const void *data, size_t length, void *ctx)
{
    http_client_connection_t connection = ctx;
    ssize_t nbytes;

    assert(connection->fd >= 0);
    assert(connection->request.istream != NULL);

    nbytes = write(connection->fd, data, length);
    if (likely(nbytes >= 0)) {
        event2_or(&connection->event, EV_WRITE);
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        event2_or(&connection->event, EV_WRITE);
        return 0;
    }

    daemon_log(1, "write error on HTTP client connection: %s\n",
               strerror(errno));
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

    event2_set(&connection->event, EV_READ);
}

static void
http_client_request_stream_abort(void *ctx)
{
    http_client_connection_t connection = ctx;

    assert(connection->request.istream != NULL);

    http_client_connection_close(connection);
}

static const struct istream_handler http_client_request_stream_handler = {
    .data = http_client_request_stream_data,
    .eof = http_client_request_stream_eof,
    .abort = http_client_request_stream_abort,
};


void
http_client_request(http_client_connection_t connection,
                    http_method_t method, const char *uri,
                    growing_buffer_t headers,
                    istream_t body,
                    const struct http_response_handler *handler,
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
    http_response_handler_set(&connection->request.handler, handler, ctx);

    /* request line */

    snprintf(connection->request.request_line_buffer,
             sizeof(connection->request.request_line_buffer),
             "%s %s HTTP/1.1\r\n",
             http_method_to_string(method), uri);

    request_line_stream
        = istream_string_new(connection->request.pool,
                             connection->request.request_line_buffer);

    /* headers */

    if (headers == NULL)
        headers = growing_buffer_new(connection->request.pool, 256);

    if (body != NULL) {
        off_t content_length = istream_available(body, 0);
        if (content_length == (off_t)-1) {
            header_write(headers, "transfer-encoding", "chunked");
            body = istream_chunked_new(connection->request.pool, body);
        } else {
            snprintf(connection->request.content_length_buffer,
                     sizeof(connection->request.content_length_buffer),
                     "%lu", (unsigned long)content_length);
            header_write(headers, "content-length",
                         connection->request.content_length_buffer);
        }
    }

    growing_buffer_write_buffer(headers, "\r\n", 2);

    header_stream = growing_buffer_istream(headers);

    /* request istream */

    connection->request.istream = istream_cat_new(connection->request.pool,
                                                  request_line_stream,
                                                  header_stream, body,
                                                  NULL);
    istream_handler_set(connection->request.istream,
                        &http_client_request_stream_handler, connection,
                        0);

    pool_ref(connection->pool);

    event2_lock(&connection->event);

    istream_read(connection->request.istream);

    event2_unlock(&connection->event);

    pool_unref(connection->pool);
}
