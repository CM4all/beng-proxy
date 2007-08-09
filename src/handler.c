/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "instance.h"
#include "http-server.h"

#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct client_connection {
    struct list_head siblings;
    pool_t pool;
    http_server_connection_t http;
};

struct translated {
    const char *path;
    const char *path_info;
};

static size_t file_response_body(struct http_server_request *request,
                                 void *buffer, size_t max_length)
{
    int fd = (int)(size_t)request->handler_ctx;
    ssize_t nbytes;

    nbytes = read(fd, buffer, max_length);
    if (nbytes < 0) {
        perror("failed to read from file");
        http_server_connection_close(request->connection);
        return 0;
    }

    if (nbytes == 0) {
        http_server_response_finish(request->connection);
        return 0;
    }

    return (size_t)nbytes;
}

static void file_response_free(struct http_server_request *request)
{
    int fd = (int)(size_t)request->handler_ctx;
    close(fd);
    request->handler_ctx = NULL;
}

static const struct http_server_request_handler file_request_handler = {
    .response_body = file_response_body,
    .free = file_response_free,
};

void
remove_connection(struct client_connection *connection)
{
    assert(connection != NULL);
    assert(connection->http != NULL);

    list_remove(&connection->siblings);

    http_server_connection_free(&connection->http);

    pool_unref(connection->pool);
}

static struct translated *
translate(struct http_server_request *request)
{
    struct translated *translated;
    char path[1024];

    /* XXX this is, of course, a huge security hole */
    snprintf(path, sizeof(path), "/var/www/%s", request->uri);

    translated = p_calloc(request->pool, sizeof(translated));
    translated->path = p_strdup(request->pool, path);
    return translated;
}

static void
my_http_server_callback(struct http_server_request *request,
                        /*const void *body, size_t body_length,*/
                        void *ctx)
{
    struct client_connection *connection = ctx;
    struct translated *translated;
    int ret, fd;
    struct stat st;
    char buffer[4096];

    if (request == NULL) {
        /* since remove_connection() might recurse here, we check if
           the connection has already been removed from the linked
           list */
        if (connection->http != NULL)
            remove_connection(connection);
        return;
    }

    (void)request;
    (void)connection;

    translated = translate(request);
    if (translated == NULL) {
        http_server_send_message(request->connection,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
        http_server_response_finish(request->connection);
        return;
    }

    if (translated == NULL || translated->path == NULL) {
        http_server_send_message(request->connection,
                                 HTTP_STATUS_NOT_FOUND,
                                 "The requested resource does not exist.");
        http_server_response_finish(request->connection);
        return;
    }

    ret = stat(translated->path, &st);
    if (ret != 0) {
        if (errno == ENOENT) {
            http_server_send_message(request->connection,
                                     HTTP_STATUS_NOT_FOUND,
                                     "The requested file does not exist.");
        } else {
            http_server_send_message(request->connection,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Internal server error");
        }
        http_server_response_finish(request->connection);
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        http_server_send_message(request->connection,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Not a regular file");
        http_server_response_finish(request->connection);
        return;
    }

    fd = open(translated->path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            http_server_send_message(request->connection,
                                     HTTP_STATUS_NOT_FOUND,
                                     "The requested file does not exist.");
        } else {
            http_server_send_message(request->connection,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Internal server error");
        }
        http_server_response_finish(request->connection);
        return;
    }

    snprintf(buffer, sizeof(buffer), "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %lu\r\n\r\n",
             (unsigned long)st.st_size);
    http_server_send(request->connection, buffer, strlen(buffer));

    request->handler = &file_request_handler;
    request->handler_ctx = (void*)(size_t)fd;
}

void
http_listener_callback(int fd,
                       const struct sockaddr *addr, socklen_t addrlen,
                       void *ctx)
{
    struct instance *instance = (struct instance*)ctx;
    pool_t pool;
    struct client_connection *connection;

    (void)addr;
    (void)addrlen;
    (void)ctx;

    pool = pool_new_linear(instance->pool, "client_connection", 8192);
    connection = p_calloc(pool, sizeof(*connection));
    connection->pool = pool;

    list_add(&connection->siblings, &instance->connections);

    connection->http = http_server_connection_new(pool, fd,
                                                  my_http_server_callback, connection);
}
