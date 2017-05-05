/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_connection.hxx"
#include "lb_log.hxx"
#include "lb_config.hxx"
#include "lb_instance.hxx"
#include "lb_tcp.hxx"
#include "strmap.hxx"
#include "http_server/http_server.hxx"
#include "drop.hxx"
#include "ssl/ssl_filter.hxx"
#include "pool.hxx"
#include "thread_socket_filter.hxx"
#include "thread_pool.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "address_string.hxx"

#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

LbConnection::LbConnection(struct pool &_pool, LbInstance &_instance,
                           const LbListenerConfig &_listener,
                           SocketAddress _client_address)
    :pool(_pool), instance(_instance), listener(_listener),
     client_address(address_to_string(pool, _client_address))
{
    if (client_address == nullptr)
        client_address = "unknown";
}

/*
 * lb_tcp_handler
 *
 */

static void
tcp_eof(void *ctx)
{
    auto *connection = (LbConnection *)ctx;

    --connection->instance.n_tcp_connections;
    lb_connection_remove(connection);
}

static void
tcp_error(const char *prefix, const char *error, void *ctx)
{
    auto *connection = (LbConnection *)ctx;

    lb_connection_log_error(3, connection, prefix, error);
    --connection->instance.n_tcp_connections;
    lb_connection_remove(connection);
}

static void
tcp_errno(const char *prefix, int error, void *ctx)
{
    auto *connection = (LbConnection *)ctx;

    lb_connection_log_errno(3, connection, prefix, error);
    --connection->instance.n_tcp_connections;
    lb_connection_remove(connection);
}

static void
tcp_exception(const char *prefix, std::exception_ptr ep, void *ctx)
{
    auto *connection = (LbConnection *)ctx;

    lb_connection_log_error(3, connection, prefix, ep);
    --connection->instance.n_tcp_connections;
    lb_connection_remove(connection);
}

static constexpr LbTcpConnectionHandler tcp_handler = {
    .eof = tcp_eof,
    .error = tcp_error,
    ._errno = tcp_errno,
    .exception = tcp_exception,
};

/*
 * public
 *
 */

LbConnection *
lb_connection_new(LbInstance &instance,
                  const LbListenerConfig &listener,
                  SslFactory *ssl_factory,
                  UniqueSocketDescriptor &&fd, SocketAddress address)
{
    /* determine the local socket address */
    StaticSocketAddress local_address = fd.GetLocalAddress();

    struct pool *pool = pool_new_linear(instance.root_pool,
                                        "client_connection",
                                        2048);
    pool_set_major(pool);

    auto *connection = NewFromPool<LbConnection>(*pool, *pool, instance,
                                                 listener, address);

    auto fd_type = FdType::FD_TCP;

    const SocketFilter *filter = nullptr;
    void *filter_ctx = nullptr;

    if (ssl_factory != nullptr) {
        try {
            connection->ssl_filter = ssl_filter_new(*ssl_factory);
        } catch (const std::runtime_error &e) {
            lb_connection_log_error(1, connection, "SSL", e);
            pool_unref(pool);
            return nullptr;
        }

        filter = &thread_socket_filter;
        filter_ctx = connection->thread_socket_filter =
            new ThreadSocketFilter(instance.event_loop,
                                   thread_pool_get_queue(instance.event_loop),
                                   &ssl_filter_get_handler(*connection->ssl_filter));
    }

    instance.connections.push_back(*connection);

    switch (listener.destination.GetProtocol()) {
    case LbProtocol::HTTP:
        connection->http = http_server_connection_new(pool, instance.event_loop,
                                                      fd.Release(), fd_type,
                                                      filter, filter_ctx,
                                                      local_address.IsDefined()
                                                      ? (SocketAddress)local_address
                                                      : nullptr,
                                                      address,
                                                      false,
                                                      *connection);
        break;

    case LbProtocol::TCP:
        ++instance.n_tcp_connections;
        lb_tcp_new(connection->pool, instance.event_loop,
                   instance.pipe_stock,
                   std::move(fd), fd_type, filter, filter_ctx, address,
                   *listener.destination.cluster,
                   instance.clusters,
                   *connection->instance.balancer,
                   tcp_handler, connection,
                   &connection->tcp);
        break;
    }

    return connection;
}

void
lb_connection_remove(LbConnection *connection)
{
    assert(connection != nullptr);
    assert(!connection->instance.connections.empty());

    auto &connections = connection->instance.connections;
    connections.erase(connections.iterator_to(*connection));

    struct pool &pool = connection->pool;
    pool_trash(&pool);
    pool_unref(&pool);
}

void
lb_connection_close(LbConnection *connection)
{
    switch (connection->listener.destination.GetProtocol()) {
    case LbProtocol::HTTP:
        assert(connection->http != nullptr);
        http_server_connection_close(connection->http);
        break;

    case LbProtocol::TCP:
        lb_tcp_close(connection->tcp);
        --connection->instance.n_tcp_connections;
        break;
    }

    lb_connection_remove(connection);
}
