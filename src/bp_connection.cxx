/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "strmap.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_server/Handler.hxx"
#include "handler.hxx"
#include "access_log.hxx"
#include "drop.hxx"
#include "system/clock.h"
#include "gerrno.h"
#include "util/Error.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>

BpConnection::BpConnection(BpInstance &_instance, struct pool &_pool,
                           const char *_listener_tag)
    :instance(_instance),
     pool(_pool),
     config(_instance.config),
     listener_tag(_listener_tag)
{
}

BpConnection::~BpConnection()
{
    if (http != nullptr)
        http_server_connection_close(http);
}

void
BpConnection::Disposer::operator()(BpConnection *c)
{
    auto &p = c->pool;
    DeleteFromPool(p, c);
    pool_trash(&p);
    pool_unref(&p);
}

void
close_connection(BpConnection *connection)
{
    auto &connections = connection->instance.connections;
    assert(!connections.empty());
    connections.erase_and_dispose(connections.iterator_to(*connection),
                                  BpConnection::Disposer());
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
    auto &connection = *(BpConnection *)ctx;

    ++connection.instance.http_request_counter;

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
    auto &connection = *(BpConnection *)ctx;

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
    auto &connection = *(BpConnection *)ctx;
    connection.http = nullptr;

    int level = 2;

    if (error->domain == errno_quark() && error->code == ECONNRESET)
        level = 4;

    daemon_log(level, "%s\n", error->message);
    g_error_free(error);

    close_connection(&connection);
}

static void
my_http_server_connection_free(void *ctx)
{
    auto &connection = *(BpConnection *)ctx;
    connection.http = nullptr;

    close_connection(&connection);
}

static constexpr HttpServerConnectionHandler my_http_server_connection_handler = {
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
new_connection(BpInstance &instance,
               SocketDescriptor &&fd, SocketAddress address,
               const char *listener_tag)
{
    struct pool *pool;

    if (instance.connections.size() >= instance.config.max_connections) {
        unsigned num_dropped = drop_some_connections(&instance);

        if (num_dropped == 0) {
            daemon_log(1, "too many connections (%zu), dropping\n",
                       instance.connections.size());
            return;
        }
    }

    /* determine the local socket address */
    const StaticSocketAddress local_address = fd.GetLocalAddress();

    pool = pool_new_linear(instance.pool, "connection", 2048);
    pool_set_major(pool);

    auto *connection = NewFromPool<BpConnection>(*pool, instance, *pool,
                                                 listener_tag);
    instance.connections.push_front(*connection);

    http_server_connection_new(pool, fd.Steal(), FdType::FD_TCP,
                               nullptr, nullptr,
                               local_address.IsDefined()
                               ? (SocketAddress)local_address
                               : nullptr,
                               address,
                               true,
                               &my_http_server_connection_handler,
                               connection,
                               &connection->http);
}
