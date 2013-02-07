/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "connection.h"
#include "instance.h"
#include "strmap.h"
#include "http-server.h"
#include "handler.h"
#include "access-log.h"
#include "drop.h"
#include "clock.h"
#include "listener.h"
#include "gerrno.h"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

static void
remove_connection(struct client_connection *connection)
{
    struct pool *pool;

    assert(connection->http != NULL);
    assert(connection->instance != NULL);
    assert(connection->instance->num_connections > 0);

    connection->http = NULL;

    list_remove(&connection->siblings);
    --connection->instance->num_connections;

    pool = connection->pool;
    pool_trash(pool);
    pool_unref(pool);
}

void
close_connection(struct client_connection *connection)
{
    assert(connection->http != NULL);

    http_server_connection_close(connection->http);
    remove_connection(connection);
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

    ++connection->instance->http_request_counter;

    connection->site_name = NULL;
    connection->request_start_time = now_us();

    handle_http_request(connection, request, async_ref);
}

static void
my_http_server_connection_log(struct http_server_request *request,
                              http_status_t status, off_t length,
                              uint64_t bytes_received, uint64_t bytes_sent,
                              void *ctx)
{
    struct client_connection *connection = ctx;

    access_log(request, connection->site_name,
               strmap_get_checked(request->headers, "referer"),
               strmap_get_checked(request->headers, "user-agent"),
               status, length,
               bytes_received, bytes_sent,
               now_us() - connection->request_start_time);
    connection->site_name = NULL;
}

static void
my_http_server_connection_error(GError *error, void *ctx)
{
    struct client_connection *connection = ctx;

    int level = 2;

    if (error->domain == errno_quark() && error->code == ECONNRESET)
        level = 4;

    daemon_log(level, "%s\n", error->message);
    g_error_free(error);

    remove_connection(connection);
}

static void
my_http_server_connection_free(void *ctx)
{
    struct client_connection *connection = ctx;

    remove_connection(connection);
}

static const struct http_server_connection_handler my_http_server_connection_handler = {
    .request = my_http_server_connection_request,
    .log = my_http_server_connection_log,
    .error = my_http_server_connection_error,
    .free = my_http_server_connection_free,
};


/*
 * listener handler
 *
 */

static void
http_listener_connected(int fd,
                        const struct sockaddr *addr, size_t addrlen,
                        void *ctx)
{
    struct instance *instance = (struct instance*)ctx;
    struct pool *pool;
    struct client_connection *connection;
    struct sockaddr_storage local_address;
    socklen_t local_address_length;
    int ret;

    if (instance->num_connections >= instance->config.max_connections) {
        unsigned num_dropped = drop_some_connections(instance);

        if (num_dropped == 0) {
            daemon_log(1, "too many connections (%u), dropping\n",
                       instance->num_connections);
            close(fd);
            return;
        }
    }

    /* determine the local socket address */
    local_address_length = sizeof(local_address);
    ret = getsockname(fd, (struct sockaddr *)&local_address,
                      &local_address_length);
    if (ret < 0)
        local_address_length = 0;

    pool = pool_new_linear(instance->pool, "client_connection", 2048);
    pool_set_major(pool);

    connection = p_malloc(pool, sizeof(*connection));
    connection->instance = instance;
    connection->pool = pool;
    connection->config = &instance->config;
    connection->site_name = NULL;

    list_add(&connection->siblings, &instance->connections);
    ++connection->instance->num_connections;

    http_server_connection_new(pool, fd, ISTREAM_TCP,
                               local_address_length > 0
                               ? (const struct sockaddr *)&local_address
                               : NULL,
                               local_address_length,
                               addr, addrlen,
                               true,
                               &my_http_server_connection_handler,
                               connection,
                               &connection->http);
}

static void
http_listener_error(GError *error, G_GNUC_UNUSED void *ctx)
{
    daemon_log(2, "%s\n", error->message);
    g_error_free(error);
}

const struct listener_handler http_listener_handler = {
    .connected = http_listener_connected,
    .error = http_listener_error,
};
