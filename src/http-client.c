/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-client.h"
#include "http-response.h"
#include "fifo-buffer.h"
#include "strutil.h"
#include "buffered-io.h"
#include "header-parser.h"
#include "header-writer.h"
#include "event2.h"
#include "http-body.h"
#include "istream-internal.h"
#include "async.h"
#include "growing-buffer.h"
#include "lease.h"

#include <inline/compiler.h>
#include <inline/poison.h>
#include <daemon/log.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct http_client_connection {
    pool_t pool, caller_pool;

    /* I/O */
    int fd;
    struct lease_ref lease_ref;
    struct event2 event;
    fifo_buffer_t input;

    /* request */
    struct {
        istream_t istream;
        char request_line_buffer[1024];
        char content_length_buffer[32];

        struct http_response_handler_ref handler;
        struct async_operation async;
    } request;

    /* response */
    struct {
        enum {
            READ_NONE,
            READ_STATUS,
            READ_HEADERS,
            READ_BODY,
            READ_ABORTED
        } read_state;
        http_status_t status;
        struct strmap *headers;
        istream_t body;
        struct http_body_reader body_reader;
    } response;

    /* connection settings */
    bool keep_alive;
#ifdef __linux
    bool cork;
#endif
};

static inline bool
http_client_connection_valid(struct http_client_connection *connection)
{
    return connection->fd >= 0;
}

static void
http_client_connection_close(struct http_client_connection *connection);

static void
http_client_consume_body(struct http_client_connection *connection);

static void
http_client_try_read(struct http_client_connection *connection);



/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
http_client_release(struct http_client_connection *client, bool reuse)
{
    assert(client != NULL);

    event2_set(&client->event, 0);
    event2_commit(&client->event);
    client->fd = -1;
    lease_release(&client->lease_ref, reuse);
    pool_unref(client->pool);
}

/**
 * Abort sending the request to the HTTP server.
 */
static void
http_client_abort_request(struct http_client_connection *client)
{
    assert(client->response.read_state == READ_NONE);
    assert(client->request.istream != NULL);

    istream_handler_clear(client->request.istream);
    istream_close(client->request.istream);

    http_response_handler_invoke_abort(&client->request.handler);
    pool_unref(client->caller_pool);

    http_client_release(client, false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
static void
http_client_abort_response_headers(struct http_client_connection *client)
{
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);
    assert(client->request.istream == NULL);

    http_response_handler_invoke_abort(&client->request.handler);
    pool_unref(client->caller_pool);

    http_client_release(client, false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
static void
http_client_abort_response_body(struct http_client_connection *client)
{
    assert(client->response.read_state == READ_BODY ||
           client->response.read_state == READ_ABORTED);
    assert(client->request.istream == NULL);

    if (client->response.read_state == READ_BODY)
        istream_deinit_abort(&client->response.body_reader.output);

    http_client_release(client, false);
}


/*
 * istream implementation for the response body
 *
 */

static inline struct http_client_connection *
response_stream_to_connection(istream_t istream)
{
    return (struct http_client_connection *)(((char*)istream) - offsetof(struct http_client_connection, response.body_reader.output));
}

static off_t
http_client_response_stream_available(istream_t istream,
                                      bool partial __attr_unused)
{
    struct http_client_connection *connection = response_stream_to_connection(istream);

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->response.read_state == READ_BODY);
    assert(!http_response_handler_defined(&connection->request.handler));

    return http_body_available(&connection->response.body_reader);
}

static void
http_client_response_stream_read(istream_t istream)
{
    struct http_client_connection *connection = response_stream_to_connection(istream);

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->response.read_state == READ_BODY);
    assert(connection->response.body_reader.output.handler != NULL);
    assert(!http_response_handler_defined(&connection->request.handler));

    pool_ref(connection->pool);

    http_client_consume_body(connection);

    if (!fifo_buffer_full(connection->input) &&
        connection->response.read_state == READ_BODY)
        http_client_try_read(connection);

    pool_unref(connection->pool);
}

static void
http_client_response_stream_close(istream_t istream)
{
    struct http_client_connection *connection = response_stream_to_connection(istream);

    assert(connection->response.read_state == READ_BODY);
    assert(connection->request.istream == NULL);
    assert(!http_response_handler_defined(&connection->request.handler));
    assert(!http_body_eof(&connection->response.body_reader));

    istream_deinit_abort(&connection->response.body_reader.output);
    http_client_release(connection, false);
}

static const struct istream http_client_response_stream = {
    .available = http_client_response_stream_available,
    .read = http_client_response_stream_read,
    .close = http_client_response_stream_close,
};


/*
static inline void
http_client_cork(struct http_client_connection *connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);

#ifdef __linux
    if (!connection->cork) {
        connection->cork = true;
        socket_set_cork(connection->fd, connection->cork);
    }
#else
    (void)connection;
#endif
}

static inline void
http_client_uncork(struct http_client_connection *connection)
{
    assert(connection != NULL);

#ifdef __linux
    if (connection->cork) {
        assert(connection->fd >= 0);
        connection->cork = false;
        socket_set_cork(connection->fd, connection->cork);
    }
#else
    (void)connection;
#endif
}
*/

/**
 * @return false if the connection is closed
 */
static bool
http_client_parse_status_line(struct http_client_connection *connection,
                              const char *line, size_t length)
{
    const char *space;

    assert(connection != NULL);
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
        http_client_abort_response_headers(connection);
        return false;
    }

    connection->response.status = (http_status_t)(((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0');
    if (unlikely(connection->response.status < 100 || connection->response.status > 599)) {
        http_client_abort_response_headers(connection);
        return false;
    }

    connection->response.read_state = READ_HEADERS;
    connection->response.headers = strmap_new(connection->pool, 64);
    return true;
}

/**
 * @return false if the connection is closed
 */
static bool
http_client_headers_finished(struct http_client_connection *connection)
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
        return true;
    }

    value = strmap_get(connection->response.headers, "transfer-encoding");
    if (value == NULL || strcasecmp(value, "chunked") != 0) {
        /* not chunked */

        value = strmap_get(connection->response.headers, "content-length");
        if (unlikely(value == NULL)) {
            if (connection->keep_alive) {
                daemon_log(2, "no Content-Length header in HTTP response\n");
                http_client_abort_response_headers(connection);
                return false;
            }
            content_length = (off_t)-1;
        } else {
            content_length = strtoul(value, &endptr, 10);
            if (unlikely(*endptr != 0 || content_length < 0)) {
                daemon_log(2, "invalid Content-Length header in HTTP response\n");
                http_client_abort_response_headers(connection);
                return false;
            }
        }
    } else {
        /* chunked */

        content_length = (off_t)-1;
    }

    /* istream_deinit() used poison_noaccess() - make it writable now
       for re-use */
    poison_undefined(&connection->response.body_reader,
                     sizeof(connection->response.body_reader));

    connection->response.body
        = http_body_init(&connection->response.body_reader,
                         &http_client_response_stream,
                         connection->pool,
                         connection->pool,
                         content_length,
                         connection->keep_alive);

    connection->response.read_state = READ_BODY;
    return true;
}

/**
 * @return false if the connection is closed
 */
static bool
http_client_handle_line(struct http_client_connection *connection,
                        const char *line, size_t length)
{
    assert(connection != NULL);
    assert(connection->response.read_state == READ_STATUS ||
           connection->response.read_state == READ_HEADERS);

    if (connection->response.read_state == READ_STATUS)
        return http_client_parse_status_line(connection, line, length);
    else if (length > 0) {
        header_parse_line(connection->pool,
                          connection->response.headers,
                          line, length);
        return true;
    } else
        return http_client_headers_finished(connection);
}

static void
http_client_response_finished(struct http_client_connection *connection);

static bool
http_client_parse_headers(struct http_client_connection *connection)
{
    const char *buffer, *buffer_end, *start, *end, *next = NULL;
    size_t length;
    bool bret;

    assert(connection != NULL);
    assert(connection->response.read_state == READ_STATUS ||
           connection->response.read_state == READ_HEADERS);

    buffer = fifo_buffer_read(connection->input, &length);
    if (buffer == NULL)
        return false;

    assert(length > 0);
    buffer_end = buffer + length;

    /* parse line by line */
    start = buffer;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        next = end + 1;

        /* strip the line */
        --end;
        if (likely(*end == '\r'))
            --end;
        while (unlikely(end >= start && char_is_whitespace(*end)))
            --end;

        /* handle this line */
        bret = http_client_handle_line(connection, start, end - start + 1);
        if (!bret)
            return false;

        if (connection->response.read_state != READ_HEADERS)
            /* header parsing is finished */
            break;

        start = next;
    }

    if (next == NULL)
        /* not a single line was processed - skip the following
           checks */
        return false;

    /* remove the parsed part of the buffer */
    fifo_buffer_consume(connection->input, next - buffer);

    if (connection->response.read_state != READ_HEADERS) {
        bool empty_response = connection->response.body == NULL;

        assert(connection->response.read_state == READ_BODY);

        http_response_handler_invoke_response(&connection->request.handler,
                                              connection->response.status,
                                              connection->response.headers,
                                              connection->response.body);
        pool_unref(connection->caller_pool);

        if (empty_response)
            http_client_response_finished(connection);
    }

    return true;
}

static void
http_client_response_finished(struct http_client_connection *connection)
{
    assert(connection->response.read_state == READ_BODY);
    assert(connection->request.istream == NULL);
    assert(!http_response_handler_defined(&connection->request.handler));

    connection->response.read_state = READ_NONE;
    connection->response.headers = NULL;
    connection->response.body = NULL;

    if (!fifo_buffer_empty(connection->input)) {
        daemon_log(2, "excess data after HTTP response\n");
        connection->keep_alive = false;
    }

    event2_set(&connection->event, 0);
    event2_commit(&connection->event);
    connection->fd = -1;
    lease_release(&connection->lease_ref, connection->keep_alive);
    pool_unref(connection->pool);
}

static void
http_client_response_stream_eof(struct http_client_connection *connection)
{
    assert(connection->response.read_state == READ_BODY);
    assert(connection->request.istream == NULL);
    assert(!http_response_handler_defined(&connection->request.handler));
    assert(http_body_eof(&connection->response.body_reader));

    istream_deinit_eof(&connection->response.body_reader.output);

    http_client_response_finished(connection);
}

static void
http_client_consume_body(struct http_client_connection *connection)
{
    size_t nbytes;

    assert(connection != NULL);
    assert(connection->response.read_state == READ_BODY);

    nbytes = http_body_consume_body(&connection->response.body_reader, connection->input);
    if (nbytes == 0 || !http_client_connection_valid(connection))
        return;

    if (http_body_eof(&connection->response.body_reader)) {
        http_client_response_stream_eof(connection);
        return;
    }

    event2_setbit(&connection->event, EV_READ, !fifo_buffer_full(connection->input));
}

static void
http_client_consume_headers(struct http_client_connection *connection)
{
    assert(connection != NULL);
    assert(connection->response.read_state == READ_STATUS ||
           connection->response.read_state == READ_HEADERS);

    do {
        if (!http_client_parse_headers(connection))
            break;
    } while (connection->response.read_state == READ_STATUS ||
             connection->response.read_state == READ_HEADERS);

    if (http_client_connection_valid(connection) &&
        connection->response.read_state == READ_BODY)
        http_client_consume_body(connection);
}

static void
http_client_try_response_direct(struct http_client_connection *connection)
{
    ssize_t nbytes;

    assert(connection->fd >= 0);
    assert(connection->response.read_state == READ_BODY);

    nbytes = http_body_try_direct(&connection->response.body_reader, connection->fd);
    if (nbytes < 0) {
        /* XXX EAGAIN? */
        daemon_log(1, "read error on HTTP connection: %s\n", strerror(errno));
        http_client_abort_response_body(connection);
        return;
    }

    if (nbytes > 0 && http_body_eof(&connection->response.body_reader))
        http_client_response_stream_eof(connection);
}

static void
http_client_try_read_buffered(struct http_client_connection *connection)
{
    ssize_t nbytes;

    nbytes = read_to_buffer(connection->fd, connection->input, INT_MAX);
    assert(nbytes != -2);

    if (nbytes == 0) {
        if (connection->response.read_state == READ_BODY) {
            connection->response.read_state = READ_ABORTED;

            http_body_socket_eof(&connection->response.body_reader,
                                 connection->input);
        }

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
        connection->response.read_state != READ_NONE) {
        event2_setbit(&connection->event, EV_READ,
                      (connection->response.body_reader.output.handler_direct & ISTREAM_SOCKET) != 0 ||
                      !fifo_buffer_full(connection->input));
    }
}

static void
http_client_try_read(struct http_client_connection *connection)
{
    if (connection->response.read_state == READ_BODY &&
        (connection->response.body_reader.output.handler_direct & ISTREAM_SOCKET) != 0 &&
        fifo_buffer_empty(connection->input))
        http_client_try_response_direct(connection);
    else
        http_client_try_read_buffered(connection);
}

static void
http_client_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct http_client_connection *connection = ctx;

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

static void
http_client_request_close(struct http_client_connection *connection)
{
    assert(connection != NULL);
    assert(connection->response.read_state == READ_BODY ||
           connection->response.read_state == READ_ABORTED ||
           http_response_handler_defined(&connection->request.handler));

    if (connection->request.istream != NULL)
        istream_free_handler(&connection->request.istream);

    if (connection->response.read_state == READ_BODY) {
        istream_deinit_abort(&connection->response.body_reader.output);
    } else if (connection->response.read_state != READ_ABORTED) {
        /* we're not reading the response yet, but we nonetheless want
           to notify the caller (callback) that the response object is
           being freed */
        http_response_handler_invoke_abort(&connection->request.handler);
        pool_unref(connection->caller_pool);
    }
}

static void
http_client_connection_close(struct http_client_connection *connection)
{
    assert(connection != NULL);

#ifdef __linux
    connection->cork = false;
#endif

    http_client_request_close(connection);
    http_client_release(connection, false);
}


/*
 * istream handler for the request
 *
 */

static size_t
http_client_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct http_client_connection *connection = ctx;
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
    http_client_abort_request(connection);
    return 0;
}

static void
http_client_request_stream_eof(void *ctx)
{
    struct http_client_connection *connection = ctx;

    assert(connection->request.istream != NULL);

    connection->request.istream = NULL;

    connection->response.read_state = READ_STATUS;
    connection->response.headers = NULL;
    connection->input = fifo_buffer_new(connection->pool, 4096);

    event2_set(&connection->event, EV_READ);
}

static void
http_client_request_stream_abort(void *ctx)
{
    struct http_client_connection *connection = ctx;

    assert(connection->request.istream != NULL);

    connection->request.istream = NULL;

    http_response_handler_invoke_abort(&connection->request.handler);
    pool_unref(connection->caller_pool);

    http_client_release(connection, false);
}

static const struct istream_handler http_client_request_stream_handler = {
    .data = http_client_request_stream_data,
    .eof = http_client_request_stream_eof,
    .abort = http_client_request_stream_abort,
};


/*
 * async operation
 *
 */

static struct http_client_connection *
async_to_http_client_connection(struct async_operation *ao)
{
    return (struct http_client_connection*)(((char*)ao) - offsetof(struct http_client_connection, request.async));
}

static void
http_client_request_abort(struct async_operation *ao)
{
    struct http_client_connection *connection
        = async_to_http_client_connection(ao);
    
    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(connection->response.read_state == READ_NONE ||
           connection->response.read_state == READ_STATUS ||
           connection->response.read_state == READ_HEADERS);

    pool_unref(connection->caller_pool);

    if (connection->response.read_state == READ_NONE) {
        assert(connection->request.istream != NULL);

        istream_handler_clear(connection->request.istream);
        istream_close(connection->request.istream);
    } else {
        assert(connection->request.istream == NULL);
    }

    http_client_release(connection, false);
}

static struct async_operation_class http_client_request_async_operation = {
    .abort = http_client_request_abort,
};


/*
 * constructor
 *
 */

void
http_client_request(pool_t caller_pool, int fd,
                    const struct lease *lease, void *lease_ctx,
                    http_method_t method, const char *uri,
                    struct growing_buffer *headers,
                    istream_t body,
                    const struct http_response_handler *handler,
                    void *ctx,
                    struct async_operation_ref *async_ref)
{
    pool_t pool;
    struct http_client_connection *connection;
    istream_t request_line_stream, header_stream;
    static const struct timeval tv = {
        .tv_sec = 30,
        .tv_usec = 0,
    };

    assert(fd >= 0);
    assert(handler != NULL);
    assert(handler->response != NULL);

    pool = pool_new_linear(caller_pool, "http_client_request", 8192);

    connection = p_malloc(pool, sizeof(*connection));
    connection->pool = pool;
    connection->fd = fd;
    lease_ref_set(&connection->lease_ref, lease, lease_ctx);

    connection->response.read_state = READ_NONE;

    event2_init(&connection->event, connection->fd,
                http_client_event_callback, connection,
                &tv);

    pool_ref(caller_pool);
    connection->caller_pool = caller_pool;
    http_response_handler_set(&connection->request.handler, handler, ctx);

    async_init(&connection->request.async, &http_client_request_async_operation);
    async_ref_set(async_ref, &connection->request.async);

    /* request line */

    snprintf(connection->request.request_line_buffer,
             sizeof(connection->request.request_line_buffer),
             "%s %s HTTP/1.1\r\n",
             http_method_to_string(method), uri);

    request_line_stream
        = istream_string_new(connection->pool,
                             connection->request.request_line_buffer);

    /* headers */

    if (headers == NULL)
        headers = growing_buffer_new(connection->pool, 256);

    if (body != NULL) {
        off_t content_length = istream_available(body, false);
        if (content_length == (off_t)-1) {
            header_write(headers, "transfer-encoding", "chunked");
            body = istream_chunked_new(connection->pool, body);
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

    connection->request.istream = istream_cat_new(connection->pool,
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
