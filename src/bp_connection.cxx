/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "strmap.hxx"
#include "http_server.hxx"
#include "handler.hxx"
#include "access_log.hxx"
#include "drop.hxx"
#include "clock.h"
#include "gerrno.h"
#include "util/Error.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>

static void
remove_connection(client_connection &connection)
{
    assert(connection.http != nullptr);
    assert(connection.instance != nullptr);
    assert(connection.instance->num_connections > 0);

    connection.http = nullptr;

    list_remove(&connection.siblings);
    --connection.instance->num_connections;

    struct pool *pool = connection.pool;
    pool_trash(pool);
    pool_unref(pool);
}

void
close_connection(struct client_connection *connection)
{
    assert(connection->http != nullptr);

    http_server_connection_close(connection->http);
    remove_connection(*connection);
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
    client_connection &connection = *(client_connection *)ctx;

    ++connection.instance->http_request_counter;

    connection.site_name = nullptr;
    connection.request_start_time = now_us();

    handle_http_request(connection, *request, *async_ref);
}

static void
my_http_server_connection_log(struct http_server_request *request,
                              http_status_t status, off_t length,
                              uint64_t bytes_received, uint64_t bytes_sent,
                              void *ctx)
{
    client_connection &connection = *(client_connection *)ctx;

    access_log(request, connection.site_name,
               strmap_get_checked(request->headers, "referer"),
               strmap_get_checked(request->headers, "user-agent"),
               status, length,
               bytes_received, bytes_sent,
               now_us() - connection.request_start_time);
    connection.site_name = nullptr;
}

static void
my_http_server_connection_error(GError *error, void *ctx)
{
    client_connection &connection = *(client_connection *)ctx;

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
    client_connection &connection = *(client_connection *)ctx;

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

void
new_connection(struct instance *instance,
               SocketDescriptor &&fd, SocketAddress address)
{
    struct pool *pool;

    if (instance->num_connections >= instance->config.max_connections) {
        unsigned num_dropped = drop_some_connections(instance);

        if (num_dropped == 0) {
            daemon_log(1, "too many connections (%u), dropping\n",
                       instance->num_connections);
            return;
        }
    }

    /* determine the local socket address */
    const StaticSocketAddress local_address = fd.GetLocalAddress();

    pool = pool_new_linear(instance->pool, "client_connection", 2048);
    pool_set_major(pool);

    client_connection *connection = NewFromPool<client_connection>(*pool);
    connection->instance = instance;
    connection->pool = pool;
    connection->config = &instance->config;
    connection->site_name = nullptr;

    list_add(&connection->siblings, &instance->connections);
    ++connection->instance->num_connections;

    http_server_connection_new(pool, fd.Steal(), ISTREAM_TCP, nullptr, nullptr,
                               local_address.IsDefined()
                               ? local_address
                               : nullptr,
                               local_address.GetSize(),
                               address, address.GetSize(),
                               true,
                               &my_http_server_connection_handler,
                               connection,
                               &connection->http);
}
