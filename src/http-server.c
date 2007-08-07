/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server.h"
#include "fifo-buffer.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct http_server_connection {
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
    int reading_headers;
};

static int
http_server_request_new(http_server_connection_t connection,
                        struct http_server_request **request_r)
{
    struct http_server_request *request;

    assert(request_r != NULL);

    request = calloc(1, sizeof(*request));
    if (request == NULL)
        return -1;

    request->connection = connection;

    *request_r = request;
    return 0;
}

static void
http_server_request_free(struct http_server_request **request_r)
{
    struct http_server_request *request = *request_r;
    *request_r = NULL;

    assert(request != NULL);

    if (request->uri != NULL)
        free(request->uri);

    free(request);
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
        char *uri;
        int ret;

        eol = line + length;

        space = memchr(line, ' ', length);
        if (space == NULL)
            return;

        method = http_parse_method_name(line, space - line);
        line = space + 1;

        space = memchr(line, ' ', eol - line);
        if (space == NULL)
            space = eol;

        uri = malloc(space - line + 1);
        if (uri == NULL)
            return; /* XXX */

        memcpy(uri, line, space - line);
        uri[space - line] = 0;

        ret = http_server_request_new(connection, &connection->request);
        if (ret < 0) {
            free(uri);
            return;
        }

        connection->request->method = method;
        connection->request->uri = uri;
        connection->reading_headers = 1;

        fprintf(stderr, "method=%d uri='%s'\n",
                connection->request->method,
                connection->request->uri);
    } else if (length > 0) {
        /* XXX parse header */
    } else {
        connection->reading_headers = 0;
        connection->callback(connection->request, connection->callback_ctx);
        /* XXX request body? */
        http_server_request_free(&connection->request);
    }
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
http_server_event_callback(int fd, short event, void *ctx)
{
    http_server_connection_t connection = ctx;
    void *buffer;
    const char *start, *end, *next, *bound;
    size_t max_length, length;
    ssize_t nbytes;

    if (event & EV_TIMEOUT) {
        fprintf(stderr, "timeout\n");
        http_server_connection_free(&connection);
        return;
    }

    if (event & EV_READ) {
        buffer = fifo_buffer_write(connection->input, &max_length);
        assert(buffer != NULL);

        assert(max_length > 0);

        nbytes = read(fd, buffer, max_length);
        if (nbytes < 0) {
            perror("read error on HTTP connection");
            http_server_connection_free(&connection);
            return;
        }

        if (nbytes == 0) {
            /* XXX */
            http_server_connection_free(&connection);
            return;
        }

        fifo_buffer_append(connection->input, (size_t)nbytes);

        if (connection->request == NULL || connection->reading_headers) {
            end = memchr(buffer, '\n', (size_t)nbytes);
            if (end == NULL)
                return;

            start = fifo_buffer_read(connection->input, &length);
            bound = start + length;

            do {
                next = end + 1;
                while (end > start && end[-1] >= 0 && end[-1] <= 0x20)
                    --end;
                http_server_handle_line(connection, start, end - start);
                fifo_buffer_consume(connection->input, next - start);

                if (connection->request != NULL && !connection->reading_headers)
                    break;

                start = next;
                end = memchr(start, '\n', bound - start);
            } while (end != NULL);
        } else {
            http_server_request_free(&connection->request);
        }
    }

    if (event & EV_WRITE) {
        start = fifo_buffer_read(connection->output, &length);
        nbytes = write(fd, start, length);
        if (nbytes < 0) {
            perror("write error on HTTP connection");
            http_server_connection_free(&connection);
            return;
        }

        fifo_buffer_consume(connection->output, length);
    }

    http_server_event_setup(connection);
}

int
http_server_connection_new(int fd,
                           http_server_callback_t callback, void *ctx,
                           http_server_connection_t *connection_r)
{
    http_server_connection_t connection;
    int ret;

    assert(fd >= 0);
    assert(callback != NULL);
    assert(connection_r != NULL);

    connection = calloc(1, sizeof(*connection));
    if (connection == NULL)
        return -1;

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

void
http_server_connection_free(http_server_connection_t *connection_r)
{
    http_server_connection_t connection = *connection_r;
    *connection_r = NULL;

    assert(connection != NULL);

    if (connection->fd >= 0)
        close(connection->fd);

    if (connection->input != NULL)
        fifo_buffer_delete(&connection->input);

    if (connection->output != NULL)
        fifo_buffer_delete(&connection->output);
}

void
http_server_send_message(http_server_connection_t connection,
                         http_status_t status, const char *msg)
{
    char header[256];
    size_t header_length, body_length, max_length;
    unsigned char *dest;

    assert(connection != NULL);
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
