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

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>

void
remove_connection(struct client_connection *connection)
{
    assert(connection != NULL);
    assert(connection->http == NULL);
    assert(connection->instance != NULL);
    assert(connection->instance->num_connections > 0);

    list_remove(&connection->siblings);
    --connection->instance->num_connections;

    pool_unref(connection->pool);
}

void
close_connection(struct client_connection *connection)
{
    http_server_connection_t http = connection->http;

    if (http != NULL)
        http_server_connection_free(&http);
}

void
http_listener_callback(int fd,
                       const struct sockaddr *addr, socklen_t addrlen,
                       void *ctx)
{
    struct instance *instance = (struct instance*)ctx;
    pool_t pool;
    struct client_connection *connection;

    if (instance->num_connections >= instance->config.max_connnections) {
        /* XXX rather drop an existing connection? */
        daemon_log(1, "too many connections (%u), dropping\n",
                   instance->num_connections);
        close(fd);
        return;
    }

    pool = pool_new_linear(instance->pool, "client_connection", 16384);
    pool_set_major(pool);

    connection = p_malloc(pool, sizeof(*connection));
    connection->instance = instance;
    connection->pool = pool;
    connection->config = &instance->config;

    list_add(&connection->siblings, &instance->connections);
    ++connection->instance->num_connections;

    http_server_connection_new(pool, fd,
                               address_to_string(pool, addr, addrlen),
                               &my_http_server_connection_handler,
                               connection,
                               &connection->http);
}
