/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server.h"
#include "fifo-buffer.h"
#include "strutil.h"

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
    int reading_headers, reading_body;
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

    pool_unref(request->pool);
}

static http_method_t
http_parse_method_name(const char *name, size_t length)
{
    if (length == 3 && memcmp(name, "GET", 3) == 0)
        return HTTP_METHOD_GET;
    if (length == 4 && memcmp(name, "POST", 4) == 0)
        return HTTP_METHOD_POST;
    return HTTP_METHOD_INVALID;
}

static void
http_server_handle_line(http_server_connection_t connection,
                        const char *line, size_t length)
{
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

        fprintf(stderr, "method=%d uri='%s'\n",
                connection->request->method,
                connection->request->uri);
    } else if (length > 0) {
        /* parse request header */
        const char *colon, *key_end;
        char *key, *value;

        while (length > 0 && char_is_whitespace(line[length - 1]))
            --length;

        colon = memchr(line, ':', length);
        if (colon == NULL || colon == line)
            return;

        key_end = colon;
        while (key_end > line && char_is_whitespace(key_end[-1]))
            --key_end;

        ++colon;
        while (colon < line + length && char_is_whitespace(*colon))
            ++colon;

        key = p_strndup(connection->request->pool, line, key_end - line);
        value = p_strndup(connection->request->pool, colon, line + length - colon);

        str_to_lower(key);

        strmap_addn(connection->request->headers, key, value);
    } else {
        connection->reading_headers = 0;
        connection->callback(connection->request, connection->callback_ctx);
        /* XXX request body? */
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
        while (end > start && char_is_whitespace(end[-1]))
            --end;

        http_server_handle_line(connection, start, end - start);
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
http_server_event_callback(int fd, short event, void *ctx);

static void
http_server_event_setup(http_server_connection_t connection)
{
    short event = 0;
    struct timeval tv;

    if (!fifo_buffer_full(connection->input))
        event = EV_READ | EV_TIMEOUT;

    if (!fifo_buffer_empty(connection->output))
        event |= EV_WRITE;

    tv.tv_sec = 30;
    tv.tv_usec = 0;

    event_set(&connection->event, connection->fd,
              event, http_server_event_callback, connection);
    event_add(&connection->event, &tv);
}

static void
http_server_connection_close(http_server_connection_t connection);

static void
http_server_event_callback(int fd, short event, void *ctx)
{
    http_server_connection_t connection = ctx;
    void *buffer;
    const char *start;
    size_t max_length, length;
    ssize_t nbytes;

    if (event & EV_TIMEOUT) {
        fprintf(stderr, "timeout\n");
        http_server_connection_close(connection);
        connection->callback(NULL, connection->callback_ctx);
        return;
    }

    if (event & EV_WRITE) {
        start = fifo_buffer_read(connection->output, &length);
        nbytes = write(fd, start, length);
        if (nbytes < 0) {
            perror("write error on HTTP connection");
            http_server_connection_close(connection);
            connection->callback(NULL, connection->callback_ctx);
            return;
        }

        fifo_buffer_consume(connection->output, length);
    }

    if (event & EV_READ) {
        buffer = fifo_buffer_write(connection->input, &max_length);
        assert(buffer != NULL);

        assert(max_length > 0);

        nbytes = read(fd, buffer, max_length);
        if (nbytes < 0) {
            perror("read error on HTTP connection");
            http_server_connection_close(connection);
            connection->callback(NULL, connection->callback_ctx);
            return;
        }

        if (nbytes == 0) {
            /* XXX */
            http_server_connection_close(connection);
            connection->callback(NULL, connection->callback_ctx);
            return;
        }

        fifo_buffer_append(connection->input, (size_t)nbytes);

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

    http_server_event_setup(connection);
}

int
http_server_connection_new(pool_t pool, int fd,
                           http_server_callback_t callback, void *ctx,
                           http_server_connection_t *connection_r)
{
    http_server_connection_t connection;
    int ret;

    assert(fd >= 0);
    assert(callback != NULL);
    assert(connection_r != NULL);

    connection = p_calloc(pool, sizeof(*connection));
    if (connection == NULL)
        return -1;

    connection->pool = pool;
    connection->fd = fd;
    connection->callback = callback;
    connection->callback_ctx = ctx;

    ret = fifo_buffer_new(4096, &connection->input);
    if (ret < 0) {
        int save_errno = errno;
        http_server_connection_free(&connection);
        errno = save_errno;
        return -1;
    }

    ret = fifo_buffer_new(4096, &connection->output);
    if (ret < 0) {
        int save_errno = errno;
        http_server_connection_free(&connection);
        errno = save_errno;
        return -1;
    }

    http_server_event_setup(connection);

    *connection_r = connection;
    return 0;
}

static void
http_server_connection_close(http_server_connection_t connection)
{
    assert(connection != NULL);

    if (connection->fd >= 0) {
        event_del(&connection->event);
        close(connection->fd);
        connection->fd = -1;
    }
}

void
http_server_connection_free(http_server_connection_t *connection_r)
{
    http_server_connection_t connection = *connection_r;
    *connection_r = NULL;

    assert(connection != NULL);

    http_server_connection_close(connection);

    if (connection->input != NULL)
        fifo_buffer_delete(&connection->input);

    if (connection->output != NULL)
        fifo_buffer_delete(&connection->output);

    if (connection->request != NULL)
        http_server_request_free(&connection->request);
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
    assert(status >= 100 && status < 600);
    assert(msg != NULL);

    body_length = strlen(msg);

    snprintf(header, sizeof(header), "HTTP/1.1 %d\r\nContent-Type: text/plain\r\nContent-Length: %u\r\n\r\n",
             status, (unsigned)body_length);
    header_length = strlen(header);

    dest = fifo_buffer_write(connection->output, &max_length);
    if (dest == NULL || max_length < header_length + body_length)
        return;

    memcpy(dest, header, header_length);
    memcpy(dest + header_length, msg, body_length);

    fifo_buffer_append(connection->output, header_length + body_length);

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
}
