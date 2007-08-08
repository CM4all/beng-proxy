/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "instance.h"
#include "http-server.h"

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

void
remove_connection(struct client_connection *connection)
{
    list_remove(&connection->siblings);

    if (connection->http != NULL)
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
    ssize_t nbytes;

    if (request == NULL) {
        remove_connection(connection);
        return;
    }

    (void)request;
    (void)connection;

    printf("in my_http_server_callback()\n");
    printf("host=%s\n", strmap_get(request->headers, "host"));

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

    while ((nbytes = read(fd, buffer, sizeof(buffer))) > 0)
        http_server_send(request->connection, buffer, (size_t)nbytes);
    close(fd);

    http_server_response_finish(request->connection);
}

void
http_listener_callback(int fd,
                       const struct sockaddr *addr, socklen_t addrlen,
                       void *ctx)
{
    struct instance *instance = (struct instance*)ctx;
    int ret;
    pool_t pool;
    struct client_connection *connection;

    (void)addr;
    (void)addrlen;
    (void)ctx;

    printf("client %d\n", fd);

    pool = pool_new_linear(instance->pool, "client_connection", 8192);
    connection = p_calloc(pool, sizeof(*connection));
    connection->pool = pool;

    list_add(&connection->siblings, &instance->connections);

    ret = http_server_connection_new(pool, fd,
                                     my_http_server_callback, connection,
                                     &connection->http);
    if (ret < 0) {
        close(fd);
        remove_connection(connection);
    }
}
