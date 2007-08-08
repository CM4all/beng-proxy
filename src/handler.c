/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "instance.h"
#include "http-server.h"

#include <unistd.h>
#include <stdio.h>

struct client_connection {
    struct list_head siblings;
    pool_t pool;
    http_server_connection_t http;
};

void
remove_connection(struct client_connection *connection)
{
    list_remove(&connection->siblings);

    if (connection->http != NULL)
        http_server_connection_free(&connection->http);

    pool_unref(connection->pool);
}

static void
my_http_server_callback(struct http_server_request *request,
                        /*const void *body, size_t body_length,*/
                        void *ctx)
{
    struct client_connection *connection = ctx;

    if (request == NULL) {
        remove_connection(connection);
        return;
    }

    (void)request;
    (void)connection;

    printf("in my_http_server_callback()\n");
    printf("host=%s\n", strmap_get(request->headers, "host"));

    http_server_send_message(request->connection, HTTP_STATUS_OK, "Hello, world!");
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
