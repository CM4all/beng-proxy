/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server.h"
#include "fifo-buffer.h"
#include "strutil.h"

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

struct http_server_connection {
    pool_t pool;
    int fd;
    /*
    struct sockaddr_storage remote_addr;
    socklen_t remote_addrlen;
    */
    http_server_callback_t callback;
    void *callback_ctx;
    struct event event;
    fifo_buffer_t input, output;
    struct http_server_request *request;
    int reading_headers, reading_body, direct_mode;
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

    pool = pool_new_linear(connection->pool, "http_server_request", 8192);
    request = p_calloc(pool, sizeof(*request));
    request->pool = pool;
    request->headers = strmap_new(pool, 64);

    request->connection = connection;

    return request;
}

static void
http_server_request_free(struct http_server_request **request_r)
{
    struct http_server_request *request = *request_r;
    *request_r = NULL;

    if (request->handler != NULL &&
        request->handler->free != NULL) {
        request->handler->free(request);
        request->handler = NULL;
    }

    pool_unref(request->pool);
}

static http_method_t
http_parse_method_name(const char *name, size_t length)
{
    if (length == 3 && memcmp(name, "GET", 3) == 0)
        return HTTP_METHOD_GET;
    if (length == 4 && memcmp(name, "POST", 4) == 0)
        return HTTP_METHOD_POST;
    if (length == 4 && memcmp(name, "HEAD", 4) == 0)
        return HTTP_METHOD_HEAD;
    return HTTP_METHOD_INVALID;
}

static inline void
http_server_cork(http_server_connection_t connection)
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
http_server_uncork(http_server_connection_t connection)
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
http_server_call_response_body(http_server_connection_t connection)
{
    void *buffer;
    size_t max_length, length;
    ssize_t nbytes;

    assert(connection != NULL);
    assert(!connection->direct_mode);
    assert(connection->request != NULL);
    assert(connection->request->handler != NULL);

    if (connection->request->handler->response_body == NULL)
        return;

    buffer = fifo_buffer_write(connection->output, &max_length);
    if (buffer == NULL)
        return;

    do {
        length = connection->request->handler->response_body(connection->request,
                                                             buffer, max_length);
        if (length == 0)
            return;

        if (fifo_buffer_empty(connection->output)) {
            /* to save time, we handle a special but very common case
               here: the output buffer is empty, and we're going to add
               data now.  since the socket is non-blocking, we immediately
               try to commit the new data to the socket */

            /* XXX does that lead to resource starvation? */

            nbytes = write(connection->fd, buffer, length);
            if (nbytes <= 0) {
                /* didn't work - postpone the new data block */
                fifo_buffer_append(connection->output, length);
                break;
            } else if (nbytes > 0 && (size_t)nbytes < length) {
                /* some was sent */
                fifo_buffer_append(connection->output, length);
                fifo_buffer_consume(connection->output, (size_t)nbytes);
                break;
            } else {
                /* everything was sent - do it again! */
            }
        } else {
            fifo_buffer_append(connection->output, length);
            break;
        }
    } while (connection->request != NULL &&
             connection->request->handler != NULL &&
             connection->request->handler->response_body != NULL);
}

static void
http_server_try_response_body(http_server_connection_t connection)
{
    const void *buffer;
    size_t length;
    ssize_t nbytes;

    assert(connection != NULL);
    assert(connection->fd >= 0);

    while ((buffer = fifo_buffer_read(connection->output, &length)) != NULL) {
        http_server_cork(connection);

        nbytes = write(connection->fd, buffer, length);
        if (nbytes < 0 && errno != EAGAIN) {
            perror("write error on HTTP connection");
            http_server_connection_close(connection);
            break;
        } else if (nbytes > 0) {
            fifo_buffer_consume(connection->output, (size_t)nbytes);
            if (connection->request != NULL &&
                !connection->direct_mode &&
                (size_t)nbytes == length)
                http_server_call_response_body(connection);
            else
                break;
        } else {
            break;
        }
    }

    if (connection->request != NULL && connection->direct_mode &&
        fifo_buffer_empty(connection->output))
        connection->request->handler->response_direct(connection->request,
                                                      connection->fd);

    http_server_uncork(connection);

    if (connection->request == NULL && !connection->keep_alive &&
        fifo_buffer_empty(connection->output))
        /* keepalive disabled and response is finished: we must close
           the connection */
        http_server_connection_close(connection);
}

static void
http_server_handle_line(http_server_connection_t connection,
                        const char *line, size_t length)
{
    assert(connection->request == NULL || connection->reading_headers);

    if (connection->request == NULL) {
        const char *eol, *space;
        http_method_t method;

        eol = line + length;

        space = memchr(line, ' ', length);
        if (space == NULL)
            return;

        method = http_parse_method_name(line, space - line);
        line = space + 1;

        space = memchr(line, ' ', eol - line);
        if (space == NULL)
            space = eol;

        connection->request = http_server_request_new(connection);
        connection->request->method = method;
        connection->request->uri = p_strndup(connection->request->pool, line, space - line);
        connection->reading_headers = 1;
    } else if (length > 0) {
        /* parse request header */
        const char *colon, *key_end;
        char *key, *value;

        colon = memchr(line, ':', length);
        if (colon == NULL || colon == line)
            return;

        key_end = colon;

        ++colon;
        if (*colon == ' ')
            ++colon;
        while (colon < line + length && char_is_whitespace(*colon))
            ++colon;

        key = p_strndup(connection->request->pool, line, key_end - line);
        value = p_strndup(connection->request->pool, colon, line + length - colon);

        str_to_lower(key);

        strmap_addn(connection->request->headers, key, value);
    } else {
        const char *header_connection;

        header_connection = strmap_get(connection->request->headers, "connection");
        connection->keep_alive = header_connection != NULL &&
            strcmp(header_connection, "keep-alive") == 0;

        connection->reading_headers = 0;
        connection->callback(connection->request, connection->callback_ctx);

        if (connection->request != NULL) {
            if (connection->request->handler == NULL) {
                fprintf(stderr, "WARNING: no handler for request\n");
                http_server_connection_close(connection);
                return;
            }

            /* XXX request body? */

            if (connection->request->handler->request_body != NULL)
                connection->request->handler->request_body(connection->request,
                                                           NULL, 0);
        }

        http_server_try_response_body(connection);
    }
}

static int
http_server_parse_headers(http_server_connection_t connection)
{
    const char *buffer, *buffer_end, *start, *end, *next = NULL;
    size_t length;

    assert(connection->request == NULL || connection->reading_headers);

    buffer = fifo_buffer_read(connection->input, &length);
    if (buffer == NULL)
        return 0;

    assert(length > 0);
    buffer_end = buffer + length;

    start = buffer;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        next = end + 1;
        --end;
        if (*end == '\r')
            --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        http_server_handle_line(connection, start, end - start + 1);
        if (!connection->reading_headers)
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
        if (connection->request == NULL || connection->reading_headers) {
            if (http_server_parse_headers(connection) == 0)
                break;
        } else if (connection->reading_body) {
            /* XXX read body*/
        } else {
            break;
        }
    }
}

static void
http_server_event_callback(int fd, short event, void *ctx);

static void
http_server_event_setup(http_server_connection_t connection)
{
    short event = 0;
    struct timeval tv;

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->input != NULL);
    assert(connection->output != NULL);

    if ((connection->keep_alive || connection->request == NULL ||
         connection->reading_headers || connection->reading_body) &&
        !fifo_buffer_full(connection->input))
        event = EV_READ | EV_TIMEOUT;

    if (connection->direct_mode ||
        !fifo_buffer_empty(connection->output))
        event |= EV_WRITE | EV_TIMEOUT;

    tv.tv_sec = 30;
    tv.tv_usec = 0;

    event_set(&connection->event, connection->fd,
              event, http_server_event_callback, connection);
    event_add(&connection->event, &tv);
}

static void
http_server_event_callback(int fd, short event, void *ctx)
{
    http_server_connection_t connection = ctx;
    void *buffer;
    size_t max_length;
    ssize_t nbytes;

    if (event & EV_TIMEOUT) {
        fprintf(stderr, "timeout\n");
        http_server_connection_close(connection);
        return;
    }

    if (event & EV_WRITE) {
        pool_ref(connection->pool);

        http_server_try_response_body(connection);

        if (connection->fd < 0) {
            pool_unref(connection->pool);
            return;
        }

        pool_unref(connection->pool);
    }

    if (event & EV_READ) {
        buffer = fifo_buffer_write(connection->input, &max_length);
        assert(buffer != NULL);

        assert(max_length > 0);

        nbytes = read(fd, buffer, max_length);
        if (nbytes < 0) {
            perror("read error on HTTP connection");
            http_server_connection_close(connection);
            return;
        }

        if (nbytes == 0) {
            /* XXX */
            http_server_connection_close(connection);
            return;
        }

        fifo_buffer_append(connection->input, (size_t)nbytes);

        pool_ref(connection->pool);

        http_server_consume_input(connection);

        if (connection->fd < 0) {
            pool_unref(connection->pool);
            return;
        }

        pool_unref(connection->pool);
    }

    http_server_event_setup(connection);
}

http_server_connection_t
http_server_connection_new(pool_t pool, int fd,
                           http_server_callback_t callback, void *ctx)
{
    http_server_connection_t connection;

    assert(fd >= 0);
    assert(callback != NULL);

    connection = p_calloc(pool, sizeof(*connection));
    connection->pool = pool;
    connection->fd = fd;
    connection->callback = callback;
    connection->callback_ctx = ctx;

    connection->input = fifo_buffer_new(pool, 4096);
    connection->output = fifo_buffer_new(pool, 4096);

    http_server_event_setup(connection);

    return connection;
}

void
http_server_connection_close(http_server_connection_t connection)
{
    assert(connection != NULL);

    if (connection->fd >= 0) {
        event_del(&connection->event);
        close(connection->fd);
        connection->fd = -1;
    }

    connection->reading_headers = 0;
    connection->reading_body = 0;
    connection->cork = 0;

    if (connection->request != NULL)
        http_server_request_free(&connection->request);

    if (connection->callback != NULL) {
        http_server_callback_t callback = connection->callback;
        void *callback_ctx = connection->callback_ctx;
        connection->callback = NULL;
        connection->callback_ctx = NULL;
        callback(NULL, callback_ctx);
    }
}

void
http_server_connection_free(http_server_connection_t *connection_r)
{
    http_server_connection_t connection = *connection_r;
    *connection_r = NULL;

    assert(connection != NULL);

    http_server_connection_close(connection);
}

size_t
http_server_send(http_server_connection_t connection, void *p, size_t length)
{
    unsigned char *dest;
    size_t max_length;

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(p != NULL);

    dest = fifo_buffer_write(connection->output, &max_length);
    if (dest == NULL)
        return 0;

    if (length > max_length)
        length = max_length;

    memcpy(dest, p, length);
    fifo_buffer_append(connection->output, length);

    return length;
}

size_t
http_server_send_status(http_server_connection_t connection, int status)
{
    char *dest;
    size_t length;

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(status >= 100 && status < 600);

    dest = fifo_buffer_write(connection->output, &length);
    snprintf(dest, length,
             "HTTP/1.1 %d\r\nServer: beng-proxy " VERSION "\r\n",
             status);
    length = strlen(dest);
    fifo_buffer_append(connection->output, length);
    return length;
}

void
http_server_send_message(http_server_connection_t connection,
                         http_status_t status, const char *msg)
{
    char header[256];
    size_t header_length, body_length, max_length;
    unsigned char *dest;

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(!connection->direct_mode);
    assert(msg != NULL);

    http_server_send_status(connection, status);

    body_length = strlen(msg);

    snprintf(header, sizeof(header), "Content-Type: text/plain\r\nContent-Length: %u\r\n\r\n",
             (unsigned)body_length);
    header_length = strlen(header);

    dest = fifo_buffer_write(connection->output, &max_length);
    if (dest == NULL || max_length < header_length + body_length)
        return;

    memcpy(dest, header, header_length);
    memcpy(dest + header_length, msg, body_length);

    fifo_buffer_append(connection->output, header_length + body_length);
}

void
http_server_response_direct_mode(http_server_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->request != NULL);
    assert(connection->request->handler != NULL);
    assert(connection->request->handler->response_direct != NULL);

    if (connection->direct_mode)
        return;

    connection->direct_mode = 1;

    /* if the output buffer is already empty, we can start the direct
       transfer right now */
    if (fifo_buffer_empty(connection->output))
        connection->request->handler->response_direct(connection->request,
                                                      connection->fd);
}

void
http_server_response_finish(http_server_connection_t connection)
{
    assert(connection->request != NULL);
    assert(!connection->reading_headers);

    if (connection->reading_body) {
        /* XXX discard rest of body? */
        connection->reading_body = 0;
    }

    http_server_request_free(&connection->request);

    connection->direct_mode = 0;
}
