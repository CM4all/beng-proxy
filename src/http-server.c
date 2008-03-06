/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "buffered-io.h"
#include "header-writer.h"
#include "date.h"
#include "direct.h"
#include "format.h"
#include "socket-util.h"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>

struct http_server_request *
http_server_request_new(http_server_connection_t connection)
{
    pool_t pool;
    struct http_server_request *request;

    assert(connection != NULL);

    pool = pool_new_linear(connection->pool, "http_server_request", 32768);
    pool_set_major(pool);
    request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->connection = connection;
    request->remote_host = connection->remote_host;
    request->headers = strmap_new(pool, 64);

    return request;
}

void
http_server_request_free(struct http_server_request **request_r)
{
    struct http_server_request *request = *request_r;
    *request_r = NULL;

    pool_unref(request->pool);
}

static inline void
http_server_cork(http_server_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);

#ifdef __linux
    if (!connection->cork) {
        connection->cork = 1;
        socket_set_cork(connection->fd, connection->cork);
    }
#endif
}

static inline void
http_server_uncork(http_server_connection_t connection)
{
    assert(connection != NULL);

#ifdef __linux
    if (connection->cork) {
        assert(connection->fd >= 0);
        connection->cork = 0;
        socket_set_cork(connection->fd, connection->cork);
    }
#endif
}


static void
http_server_try_write(http_server_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->request.read_state != READ_START &&
           connection->request.read_state != READ_HEADERS);
    assert(connection->request.request != NULL);
    assert(connection->response.istream != NULL);

    http_server_cork(connection);
    event2_lock(&connection->event);
    istream_read(connection->response.istream);
    event2_unlock(&connection->event);
    http_server_uncork(connection);
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
        daemon_log(4, "timeout\n");
        http_server_connection_close(connection);
    }

    if (http_server_connection_valid(connection) && (event & EV_WRITE) != 0)
        http_server_try_write(connection);

    if (http_server_connection_valid(connection) && (event & EV_READ) != 0)
        http_server_try_read(connection);

    event2_unlock(&connection->event);

    pool_unref(connection->pool);
    pool_commit();
}

void
http_server_connection_new(pool_t pool, int fd,
                           const char *remote_host,
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
    connection->remote_host = remote_host;
    connection->request.read_state = READ_START;
    connection->request.request = NULL;
    connection->response.istream = NULL;
#ifdef __linux
    connection->cork = 0;
#endif

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
http_server_request_close(struct http_server_connection *connection)
{
    pool_t pool;

    assert(connection->request.read_state != READ_START);
    assert(connection->request.request != NULL);

    pool = connection->request.request->pool;
    assert(pool != NULL);
    pool_ref(pool);

    http_server_request_free(&connection->request.request);

    connection->request.read_state = READ_START;

    if (connection->response.istream != NULL)
        istream_free_handler(&connection->response.istream);

    pool_unref(pool);
}

void
http_server_connection_close(http_server_connection_t connection)
{
    assert(connection != NULL);

    if (connection->fd >= 0) {
        event2_set(&connection->event, 0);
        close(connection->fd);
        connection->fd = -1;
    }

#ifdef __linux
    connection->cork = 0;
#endif

    pool_ref(connection->pool);

    if (connection->request.read_state != READ_START)
        http_server_request_close(connection);

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


void
http_server_maybe_send_100_continue(http_server_connection_t connection)
{
    assert(connection->fd >= 0);
    assert(connection->request.read_state == READ_BODY);

    if (!connection->request.expect_100_continue)
        return;

    assert(connection->response.istream == NULL);

    connection->request.expect_100_continue = 0;

    connection->response.istream = istream_string_new(connection->request.request->pool,
                                                      "100 Continue\r\n\r\n");
    istream_handler_set(connection->response.istream,
                        &http_server_response_stream_handler, connection,
                        ISTREAM_DIRECT_SUPPORT);

    connection->response.writing_100_continue = 1;

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
                     growing_buffer_t headers,
                     istream_t body)
{
    http_server_connection_t connection = request->connection;
    off_t content_length;
    istream_t status_stream, header_stream;

    assert(connection->request.request == request);
    assert(connection->response.istream == NULL);
    /* XXX what if we weren't able to send "100 Continue" yet? */

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
        ? 0 : istream_available(body, 0);
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

    connection->response.writing_100_continue = 0;

    pool_ref(connection->pool);

    http_server_try_write(connection);

    pool_unref(connection->pool);
}

void
http_server_send_message(struct http_server_request *request,
                         http_status_t status, const char *msg)
{
    growing_buffer_t headers = growing_buffer_new(request->pool, 40);
    header_write(headers, "content-type", "text/plain");

    http_server_response(request, status, headers,
                         istream_string_new(request->pool, msg));
}

void
http_server_send_redirect(struct http_server_request *request,
                          http_status_t status, const char *location,
                          const char *msg)
{
    growing_buffer_t headers;

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
