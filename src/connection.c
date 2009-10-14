/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "connection.h"
#include "instance.h"
#include "http-server.h"
#include "handler.h"
#include "address.h"
#include "access-log.h"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>

void
close_connection(struct client_connection *connection)
{
    assert(connection->http != NULL);

    http_server_connection_close(connection->http);
}


/*
 * http connection handler
 *
 */

static void
my_http_server_connection_request(struct http_server_request *request,
                                  void *ctx,
                                  struct async_operation_ref *async_ref)
{
    struct client_connection *connection = ctx;

    handle_http_request(connection, request, async_ref);
}

static void
my_http_server_connection_log(struct http_server_request *request,
                              http_status_t status, off_t length,
                              void *ctx __attr_unused)
{
    access_log(request, status, length);
}

static void
my_http_server_connection_free(void *ctx)
{
    struct client_connection *connection = ctx;
    pool_t pool;

    assert(connection->http != NULL);
    assert(connection->instance != NULL);
    assert(connection->instance->num_connections > 0);

    connection->http = NULL;

    list_remove(&connection->siblings);
    --connection->instance->num_connections;

    pool = connection->pool;
    pool_unref(pool);
    pool_trash(pool);
}

static const struct http_server_connection_handler my_http_server_connection_handler = {
    .request = my_http_server_connection_request,
    .log = my_http_server_connection_log,
    .free = my_http_server_connection_free,
};


/*
 * listener callback
 *
 */

void
http_listener_callback(int fd,
                       const struct sockaddr *addr, socklen_t addrlen,
                       void *ctx)
{
    struct instance *instance = (struct instance*)ctx;
    pool_t pool;
    struct client_connection *connection;
    struct sockaddr_storage local_address;
    socklen_t local_address_length;
    int ret;

    if (instance->num_connections >= instance->config.max_connections) {
        /* XXX rather drop an existing connection? */
        daemon_log(1, "too many connections (%u), dropping\n",
                   instance->num_connections);
        close(fd);
        return;
    }

    /* determine the local socket address */
    local_address_length = sizeof(local_address);
    ret = getsockname(fd, (struct sockaddr *)&local_address,
                      &local_address_length);
    if (ret < 0)
        local_address_length = 0;

    pool = pool_new_linear(instance->pool, "client_connection", 16384);
    pool_set_major(pool);

    connection = p_malloc(pool, sizeof(*connection));
    connection->instance = instance;
    connection->pool = pool;
    connection->config = &instance->config;

    list_add(&connection->siblings, &instance->connections);
    ++connection->instance->num_connections;

    http_server_connection_new(pool, fd, ISTREAM_TCP,
                               local_address_length > 0
                               ? (const struct sockaddr *)&local_address
                               : NULL,
                               local_address_length,
                               address_to_string(pool, addr, addrlen),
                               &my_http_server_connection_handler,
                               connection,
                               &connection->http);
}
