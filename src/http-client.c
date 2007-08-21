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
    struct event event;
    fifo_buffer_t input, output;

    /* callback */
    http_client_callback_t callback;
    void *callback_ctx;

    /* request */
    int writing_headers;
    strmap_t request_headers;
    const struct pair *next_request_header;

    /* response */
    struct http_client_response *response;
    int reading_headers, reading_body;
    off_t body_rest;

    /* connection settings */
    int keep_alive;
    int direct_mode;
#ifdef __linux
    int cork;
#endif
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

static inline int
http_client_connection_valid(http_client_connection_t connection)
{
    return connection->fd >= 0;
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

static size_t
append_headers(http_client_connection_t connection,
               char *dest, size_t max_length)
{
    const struct pair *current = connection->next_request_header;
    size_t length = 0, key_length, value_length;

    assert(connection != NULL);
    assert(connection->writing_headers);

    /* we always want enough room for the trailing \r\n */
    if (unlikely(max_length < 2))
        return 0;

    if (current == NULL && connection->request_headers != NULL) {
        strmap_rewind(connection->request_headers);
        current = strmap_next(connection->request_headers);
    }

    while (current != NULL) {
        key_length = strlen(current->key);
        value_length = strlen(current->value);

        if (unlikely(length + key_length + 2 + value_length + 2 + 2 > max_length))
            break;

        memcpy(dest + length, current->key, key_length);
        length += key_length;
        dest[length++] = ':';
        dest[length++] = ' ';
        memcpy(dest + length, current->value, value_length);
        length += value_length;
        dest[length++] = '\r';
        dest[length++] = '\n';

        current = strmap_next(connection->request_headers);
    }

    connection->next_request_header = current;
    if (current == NULL) {
        assert(length + 2 <= max_length);
        dest[length++] = '\r';
        dest[length++] = '\n';
        connection->request_headers = NULL;
    }

    return length;
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
    ssize_t rest;

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(fifo_buffer_empty(connection->input));

    while ((rest = write_from_buffer(connection->fd,
                                     connection->output)) == 0) {
        if (connection->writing_headers) {
            char *buffer;
            size_t max_length, length;
            
            buffer = fifo_buffer_write(connection->output, &max_length);
            if (buffer != NULL) {
                length = append_headers(connection, buffer, max_length);
                fifo_buffer_append(connection->output, length);
            }
        } else {
            http_client_call_request_body(connection);
            if (connection->response == NULL)
                return;
        }
    }

    if (unlikely(rest == -1)) {
        perror("write error on HTTP connection");
        http_client_connection_close(connection);
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

    if (unlikely(length < 3 || !char_is_digit(line[0]) ||
                 !char_is_digit(line[1]) || !char_is_digit(line[2]))) {
        fprintf(stderr, "no HTTP status found\n");
        http_client_connection_close(connection);
        return -1;
    }

    status = ((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0';
    if (unlikely(status < 100 || status > 599)) {
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
    if (unlikely(colon == NULL || colon == line))
        return;

    key_end = colon;

    ++colon;
    if (likely(*colon == ' '))
        ++colon;
    while (unlikely(colon < line + length && char_is_whitespace(*colon)))
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
    if (unlikely(value == NULL)) {
        fprintf(stderr, "no Content-Length header in HTTP response\n");
        http_client_connection_close(connection);
        return;
    } else {
        connection->response->content_length = strtoul(value, &endptr, 10);
        if (unlikely(*endptr != 0 || connection->response->content_length < 0)) {
            fprintf(stderr, "invalid Content-Length header in HTTP response\n");
            http_client_connection_close(connection);
            return;
        }

        connection->body_rest = connection->response->content_length;
    }

    connection->reading_headers = 0;
    connection->reading_body = 1;
}

static void
http_client_handle_line(http_client_connection_t connection,
                        const char *line, size_t length)
{
    assert(connection->response != NULL);

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
        if (likely(*end == '\r'))
            --end;
        while (unlikely(end >= start && char_is_whitespace(*end)))
            --end;

        http_client_handle_line(connection, start, end - start + 1);
        if (!connection->reading_headers)
            break;

        start = next;
    }

    if (next == NULL)
        return 0;

    fifo_buffer_consume(connection->input, next - buffer);

    if (http_client_connection_valid(connection) &&
        !connection->reading_headers) {
        connection->callback(connection->response, connection->callback_ctx);

        if (connection->response != NULL) {
            if (unlikely(connection->response->handler == NULL)) {
                fprintf(stderr, "WARNING: no handler for request\n");
                http_client_connection_close(connection);
                return 0;
            }
        }
    }

    return 1;
}

static void
http_client_consume_body(http_client_connection_t connection)
{
    const void *buffer;
    size_t length, consumed;

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

    consumed = connection->response->handler->response_body(connection->response,
                                                            buffer, length);
    assert(consumed <= length);

    if (consumed > 0) {
        fifo_buffer_consume(connection->input, consumed);

        connection->body_rest -= (off_t)consumed;
        if (connection->body_rest <= 0)
            http_client_response_finish(connection);
    }
}

static void
http_client_consume_input(http_client_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->response != NULL);

    do {
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
http_client_try_read(http_client_connection_t connection)
{
    void *buffer;
    size_t max_length;
    ssize_t nbytes;

    if (connection->direct_mode &&
        fifo_buffer_empty(connection->input)) {
        assert(connection->response->handler->response_direct != NULL);
        connection->response->handler->response_direct(connection->response,
                                                       connection->fd);
    } else {
        buffer = fifo_buffer_write(connection->input, &max_length);
        assert(buffer != NULL);

        assert(max_length > 0);

        nbytes = read(connection->fd, buffer, max_length);
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

        http_client_consume_input(connection);
    }
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

    event_del(&connection->event);

    if (connection->response != NULL &&
        (connection->direct_mode ||
         fifo_buffer_empty(connection->input)))
        event = EV_READ | EV_TIMEOUT;

    if (!fifo_buffer_empty(connection->output))
        event |= EV_WRITE | EV_TIMEOUT;

    if (event == 0)
        return;

    tv.tv_sec = 30;
    tv.tv_usec = 0;

    event_set(&connection->event, connection->fd,
              event, http_client_event_callback, connection);
    event_add(&connection->event, &tv);
}

static void
http_client_event_callback(int fd, short event, void *ctx)
{
    http_client_connection_t connection = ctx;

    (void)fd;

    pool_ref(connection->pool);

    if (unlikely(event & EV_TIMEOUT)) {
        fprintf(stderr, "timeout\n");
        http_client_connection_close(connection);
        return;
    }

    if (http_client_connection_valid(connection) && (event & EV_WRITE) != 0)
        http_client_try_send(connection);

    if (http_client_connection_valid(connection) && (event & EV_READ) != 0)
        http_client_try_read(connection);

    if (likely(http_client_connection_valid(connection)))
        http_client_event_setup(connection);

    pool_unref(connection->pool);
    pool_commit();
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

    connection->writing_headers = 0;
    connection->request_headers = NULL;
    connection->next_request_header = NULL;
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
                    http_method_t method, const char *uri,
                    strmap_t headers)
{
    char *buffer;
    size_t max_length, length;

    assert(connection != NULL);
    assert(!connection->writing_headers);
    assert(connection->request_headers == NULL);
    assert(connection->next_request_header == NULL);
    assert(connection->response == NULL);

    connection->writing_headers = 1;
    connection->request_headers = headers;

    buffer = fifo_buffer_write(connection->output, &max_length);
    assert(buffer != NULL); /* XXX */

    (void)method; /* XXX */
    snprintf(buffer, max_length, "GET %s HTTP/1.1\r\nHost: localhost\r\n", uri);
    length = strlen(buffer);

    length += append_headers(connection, buffer + length, max_length - length);

    buffered_quick_write(connection->fd, connection->output,
                         buffer, length);

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
http_client_response_read(http_client_connection_t connection)
{
    pool_ref(connection->pool);

    http_client_consume_body(connection);

    if (connection->fd >= 0)
        http_client_event_setup(connection);

    pool_unref(connection->pool);
}

void
http_client_response_finish(http_client_connection_t connection)
{
    struct http_client_response *response;

    assert(connection->response != NULL);
    assert(!connection->reading_headers);

    connection->writing_headers = 0;
    connection->request_headers = NULL;
    connection->next_request_header = NULL;

    if (connection->reading_headers) {
        /* XXX discard rest of the headers? */
        connection->reading_headers = 0;
    }

    if (connection->reading_body) {
        /* XXX discard rest of body? */
        connection->reading_body = 0;
    }

    connection->direct_mode = 0;

    response = connection->response;
    connection->response = NULL;

    if (response->handler->response_finished != NULL)
        response->handler->response_finished(response);

    http_client_response_free(&response);
}
