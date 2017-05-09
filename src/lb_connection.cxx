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
                           UniqueSocketDescriptor &&fd, FdType fd_type,
                           const SocketFilter *filter, void *filter_ctx,
                           SocketAddress _client_address)
    :pool(_pool), instance(_instance), listener(_listener),
     client_address(address_to_string(pool, _client_address)),
     tcp(lb_tcp_new(pool, instance.event_loop, instance.pipe_stock,
                    std::move(fd), fd_type,
                    filter, filter_ctx, _client_address,
                    *listener.destination.cluster,
                    instance.clusters,
                    *instance.balancer,
                    *this))
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
 * LbTcpConnectionHandler
 *
 */

void
LbConnection::OnTcpEnd()
{
    lb_connection_remove(this);
}

void
LbConnection::OnTcpError(const char *prefix, const char *error)
{
    LogPrefix(3, prefix, error);
    lb_connection_remove(this);
}

void
LbConnection::OnTcpErrno(const char *prefix, int error)
{
    LogErrno(3, prefix, error);
    lb_connection_remove(this);
}

void
LbConnection::OnTcpError(const char *prefix, std::exception_ptr ep)
{
    Log(3, prefix, ep);
    lb_connection_remove(this);
}

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

    auto fd_type = FdType::FD_TCP;

    const SocketFilter *filter = nullptr;
    void *filter_ctx = nullptr;

    if (ssl_factory != nullptr) {
        auto *ssl_filter = ssl_filter_new(*ssl_factory);

        filter = &thread_socket_filter;
        filter_ctx =
            new ThreadSocketFilter(instance.event_loop,
                                   thread_pool_get_queue(instance.event_loop),
                                   &ssl_filter_get_handler(*ssl_filter));
    }

    struct pool *pool = pool_new_linear(instance.root_pool,
                                        "client_connection",
                                        2048);
    pool_set_major(pool);

    auto *connection = NewFromPool<LbConnection>(*pool, *pool, instance,
                                                 listener,
                                                 std::move(fd), fd_type,
                                                 filter, filter_ctx,
                                                 address);

    instance.tcp_connections.push_back(*connection);

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
