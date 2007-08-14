/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-client.h"
#include "fifo-buffer.h"
#include "strutil.h"
#include "compiler.h"

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
    int fd;
    http_client_callback_t callback;
    void *callback_ctx;
    struct event event;
    fifo_buffer_t input, output;
    struct http_client_response *response;
    int reading_headers, reading_body, direct_mode;
    int keep_alive;
#ifdef __linux
    int cork;
#endif
    off_t body_rest;
};

static struct http_client_response *
http_client_response_new(http_client_connection_t connection)
{
    pool_t pool;
    struct http_client_response *request;

    assert(connection != NULL);

    pool = pool_new_linear(connection->pool, "http_client_response", 8192);
    request = p_calloc(pool, sizeof(*request));
    request->pool = pool;
    request->headers = strmap_new(pool, 64);

    request->connection = connection;

    return request;
}

static void
http_client_response_free(struct http_client_response **request_r)
{
    struct http_client_response *request = *request_r;
    *request_r = NULL;

    if (request->handler != NULL &&
        request->handler->free != NULL) {
        request->handler->free(request);
        request->handler = NULL;
    }

    pool_unref(request->pool);
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
http_client_call_request_body(http_client_connection_t connection)
{
    /* XXX */
    (void)connection;
}

static void
http_client_try_send(http_client_connection_t connection)
{
    const void *buffer;
    size_t length;
    ssize_t nbytes;

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(fifo_buffer_empty(connection->input));

    while ((buffer = fifo_buffer_read(connection->output, &length)) != NULL) {
        nbytes = write(connection->fd, buffer, length);
        if (nbytes < 0 && errno != EAGAIN) {
            perror("write error on HTTP connection");
            http_client_connection_close(connection);
            break;
        } else if (nbytes > 0) {
            fifo_buffer_consume(connection->output, (size_t)nbytes);
            if (connection->response != NULL && (size_t)nbytes == length)
                http_client_call_request_body(connection);
            else
                break;
        } else {
            break;
        }
    }
}

static int
http_client_parse_status_line(http_client_connection_t connection,
                              const char *line, size_t length)
{
    const char *space;
    int status;

    if (length > 4 && memcmp(line, "HTTP", 4) == 0) {
        space = memchr(line + 4, ' ', length - 4);
        if (space != NULL) {
            length -= space - line + 1;
            line = space + 1;
        }
    }

    if (length < 3 || !char_is_digit(line[0]) ||
        !char_is_digit(line[1]) || !char_is_digit(line[2])) {
        fprintf(stderr, "no HTTP status found\n");
        http_client_connection_close(connection);
        return -1;
    }

    status = ((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0';
    if (status < 100 || status > 599) {
        http_client_connection_close(connection);
        return -1;
    }

    connection->reading_headers = 1;

    /* XXX */

    return status;
}

static void
http_client_parse_header_line(http_client_connection_t connection,
                              const char *line, size_t length)
{
    const char *colon, *key_end;
    char *key, *value;

    assert(connection != NULL);
    assert(connection->response != NULL);
    assert(connection->response->headers != NULL);

    colon = memchr(line, ':', length);
    if (colon == NULL || colon == line)
        return;

    key_end = colon;

    ++colon;
    if (*colon == ' ')
        ++colon;
    while (colon < line + length && char_is_whitespace(*colon))
        ++colon;

    key = p_strndup(connection->response->pool, line, key_end - line);
    value = p_strndup(connection->response->pool, colon, line + length - colon);

    str_to_lower(key);

    strmap_addn(connection->response->headers, key, value);
}

static void
http_client_headers_finished(http_client_connection_t connection)
{
    const char *header_connection, *value;
    char *endptr;

    header_connection = strmap_get(connection->response->headers, "connection");
    connection->keep_alive = header_connection != NULL &&
        strcasecmp(header_connection, "keep-alive") == 0;

    value = strmap_get(connection->response->headers, "content-length");
    if (value == NULL) {
        fprintf(stderr, "no Content-Length header in HTTP response\n");
        http_client_connection_close(connection);
        return;
    } else {
        connection->response->content_length = strtoul(value, &endptr, 10);
        if (*endptr != 0 || connection->response->content_length < 0) {
            fprintf(stderr, "invalid Content-Length header in HTTP response\n");
            http_client_connection_close(connection);
            return;
        }

        connection->body_rest = connection->response->content_length;
    }

    connection->reading_headers = 0;
    connection->reading_body = 1;
    connection->callback(connection->response, connection->callback_ctx);

    if (connection->response != NULL) {
        if (connection->response->handler == NULL) {
            fprintf(stderr, "WARNING: no handler for request\n");
            http_client_connection_close(connection);
            return;
        }
    }
}

static void
http_client_handle_line(http_client_connection_t connection,
                        const char *line, size_t length)
{
    assert(connection->response != NULL);

    printf("handle_line('%.*s')\n", (int)length, line);
    if (!connection->reading_headers) {
        http_client_parse_status_line(connection, line, length);
    } else if (length > 0) {
        http_client_parse_header_line(connection, line, length);
    } else {
        http_client_headers_finished(connection);
    }
}

static int
http_client_parse_headers(http_client_connection_t connection)
{
    const char *buffer, *buffer_end, *start, *end, *next = NULL;
    size_t length;

    assert(connection->response != NULL);

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

        http_client_handle_line(connection, start, end - start + 1);
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
http_client_consume_body(http_client_connection_t connection)
{
    const void *buffer;
    size_t length;

    assert(connection != NULL);
    assert(connection->response != NULL);
    assert(connection->reading_body);
    assert(connection->body_rest >= 0);

    /* XXX */

    buffer = fifo_buffer_read(connection->input, &length);
    if (buffer == NULL)
        return;

    if ((off_t)length > connection->body_rest)
        length = (size_t)connection->body_rest;

    printf("http_client_consume_body(%zu)\n", length);
    connection->response->handler->response_body(connection->response,
                                                 buffer, length);

    fifo_buffer_consume(connection->input, length);

    connection->body_rest -= (off_t)length;
    if (connection->body_rest <= 0)
        http_client_response_finish(connection);
}

static void
http_client_consume_input(http_client_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->response != NULL);

    do {
        printf("response=%p reading_headers=%d reading_body=%d\n",
               (void*)connection->response, connection->reading_headers,
               connection->reading_body);
        if (!connection->reading_body) {
            if (http_client_parse_headers(connection) == 0)
                break;
        } else {
            http_client_consume_body(connection);
            break;
        }
    } while (connection->response != NULL);
}

static void
http_client_event_callback(int fd, short event, void *ctx);

static void
http_client_event_setup(http_client_connection_t connection)
{
    short event = 0;
    struct timeval tv;

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->input != NULL);
    assert(connection->output != NULL);

    if (connection->response != NULL &&
        (connection->direct_mode ||
         fifo_buffer_empty(connection->input)))
        event = EV_READ | EV_TIMEOUT;

    if (!fifo_buffer_empty(connection->output))
        event |= EV_WRITE | EV_TIMEOUT;

    tv.tv_sec = 30;
    tv.tv_usec = 0;

    event_del(&connection->event);
    event_set(&connection->event, connection->fd,
              event, http_client_event_callback, connection);
    event_add(&connection->event, &tv);
}

static void
http_client_event_callback(int fd, short event, void *ctx)
{
    http_client_connection_t connection = ctx;
    void *buffer;
    size_t max_length;
    ssize_t nbytes;

    if (event & EV_TIMEOUT) {
        fprintf(stderr, "timeout\n");
        http_client_connection_close(connection);
        return;
    }

    if (event & EV_WRITE) {
        pool_lock(connection->pool);

        http_client_try_send(connection);

        if (connection->fd < 0) {
            pool_unlock(connection->pool);
            return;
        }

        pool_unlock(connection->pool);
    }

    if (event & EV_READ) {
        if (connection->direct_mode &&
            fifo_buffer_empty(connection->input)) {
            assert(connection->response->handler->response_direct != NULL);
            connection->response->handler->response_direct(connection->response,
                                                           connection->fd);
        } else {
            buffer = fifo_buffer_write(connection->input, &max_length);
            assert(buffer != NULL);

            assert(max_length > 0);

            nbytes = read(fd, buffer, max_length);
            if (nbytes < 0) {
                perror("read error on HTTP connection");
                http_client_connection_close(connection);
                return;
            }

            if (nbytes == 0) {
                /* XXX */
                http_client_connection_close(connection);
                return;
            }

            fifo_buffer_append(connection->input, (size_t)nbytes);

            pool_lock(connection->pool);

            http_client_consume_input(connection);

            if (connection->fd < 0) {
                pool_unlock(connection->pool);
                return;
            }

            pool_unlock(connection->pool);
        }
    }

    http_client_event_setup(connection);
}

http_client_connection_t
http_client_connection_new(pool_t pool, int fd,
                           http_client_callback_t callback, void *ctx)
{
    http_client_connection_t connection;

    assert(fd >= 0);
    assert(callback != NULL);

    connection = p_calloc(pool, sizeof(*connection));
    connection->pool = pool;
    connection->fd = fd;
    connection->callback = callback;
    connection->callback_ctx = ctx;

    connection->input = fifo_buffer_new(pool, 4096);
    connection->output = fifo_buffer_new(pool, 4096);

    http_client_event_setup(connection);

    return connection;
}

void
http_client_connection_close(http_client_connection_t connection)
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

    if (connection->response != NULL)
        http_client_response_free(&connection->response);

    if (connection->callback != NULL) {
        http_client_callback_t callback = connection->callback;
        void *callback_ctx = connection->callback_ctx;
        connection->callback = NULL;
        connection->callback_ctx = NULL;
        callback(NULL, callback_ctx);
    }
}

void
http_client_request(http_client_connection_t connection,
                    http_method_t method, const char *uri)
{
    char *buffer;
    size_t max_length;

    assert(connection != NULL);
    assert(connection->response == NULL);

    buffer = fifo_buffer_write(connection->output, &max_length);
    assert(buffer != NULL); /* XXX */

    (void)method; /* XXX */
    snprintf(buffer, max_length, "GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n", uri);
    fifo_buffer_append(connection->output, strlen(buffer));

    connection->response = http_client_response_new(connection);

    http_client_event_setup(connection);
}

void
http_client_response_direct_mode(http_client_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->response != NULL);
    assert(connection->response->handler != NULL);
    assert(connection->response->handler->response_direct != NULL);

    if (connection->direct_mode)
        return;

    connection->direct_mode = 1;

    /* if the output buffer is already empty, we can start the direct
       transfer right now */
    if (fifo_buffer_empty(connection->output))
        connection->response->handler->response_direct(connection->response,
                                                      connection->fd);
}

void
http_client_response_finish(http_client_connection_t connection)
{
    assert(connection->response != NULL);
    assert(!connection->reading_headers);

    if (connection->reading_headers) {
        /* XXX discard rest of the headers? */
        connection->reading_headers = 0;
    }

    if (connection->reading_body) {
        /* XXX discard rest of body? */
        connection->reading_body = 0;
    }

    http_client_response_free(&connection->response);

    connection->direct_mode = 0;
}
