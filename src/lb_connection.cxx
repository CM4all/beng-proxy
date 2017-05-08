/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_connection.hxx"
#include "lb_config.hxx"
#include "lb_instance.hxx"
#include "lb_tcp.hxx"
#include "ssl/ssl_filter.hxx"
#include "pool.hxx"
#include "thread_socket_filter.hxx"
#include "thread_pool.hxx"
#include "net/SocketAddress.hxx"
#include "address_string.hxx"

#include <assert.h>

LbConnection::LbConnection(struct pool &_pool, LbInstance &_instance,
                           const LbListenerConfig &_listener,
                           SocketAddress _client_address)
    :pool(_pool), instance(_instance), listener(_listener),
     client_address(address_to_string(pool, _client_address))
{
    if (client_address == nullptr)
        client_address = "unknown";
}

std::string
LbConnection::MakeLogName() const noexcept
{
    return "listener='" + listener.name
        + "' cluster='" + listener.destination.GetName()
        + "' client='" + client_address
        + "'";
}

/*
 * lb_tcp_handler
 *
 */

static void
tcp_eof(void *ctx)
{
    auto *connection = (LbConnection *)ctx;

    lb_connection_remove(connection);
}

static void
tcp_error(const char *prefix, const char *error, void *ctx)
{
    auto *connection = (LbConnection *)ctx;

    connection->LogPrefix(3, prefix, error);
    lb_connection_remove(connection);
}

static void
tcp_errno(const char *prefix, int error, void *ctx)
{
    auto *connection = (LbConnection *)ctx;

    connection->LogErrno(3, prefix, error);
    lb_connection_remove(connection);
}

static void
tcp_exception(const char *prefix, std::exception_ptr ep, void *ctx)
{
    auto *connection = (LbConnection *)ctx;

    connection->Log(3, prefix, ep);
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
    assert(listener.destination.GetProtocol() == LbProtocol::TCP);

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
            connection->Log(1, "Failed to create SSL filter", e);
            DeleteUnrefTrashPool(*pool, connection);
            return nullptr;
        }

        filter = &thread_socket_filter;
        filter_ctx = connection->thread_socket_filter =
            new ThreadSocketFilter(instance.event_loop,
                                   thread_pool_get_queue(instance.event_loop),
                                   &ssl_filter_get_handler(*connection->ssl_filter));
    }

    instance.tcp_connections.push_back(*connection);

    lb_tcp_new(connection->pool, instance.event_loop,
               instance.pipe_stock,
               std::move(fd), fd_type, filter, filter_ctx, address,
               *listener.destination.cluster,
               instance.clusters,
               *connection->instance.balancer,
               tcp_handler, connection,
               &connection->tcp);

    return connection;
}

void
lb_connection_remove(LbConnection *connection)
{
    assert(connection != nullptr);
    assert(!connection->instance.tcp_connections.empty());

    auto &connections = connection->instance.tcp_connections;
    connections.erase(connections.iterator_to(*connection));

    DeleteUnrefTrashPool(connection->pool, connection);
}

void
lb_connection_close(LbConnection *connection)
{
    assert(connection->listener.destination.GetProtocol() == LbProtocol::TCP);

    lb_tcp_close(connection->tcp);

    lb_connection_remove(connection);
}
