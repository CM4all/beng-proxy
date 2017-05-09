/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_tcp.hxx"
#include "lb_config.hxx"
#include "lb_cluster.hxx"
#include "client_balancer.hxx"
#include "address_sticky.hxx"
#include "direct.hxx"
#include "pool.hxx"
#include "GException.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"

#include <unistd.h>
#include <errno.h>

static constexpr timeval write_timeout = { 30, 0 };

void
LbTcpConnection::DestroyInbound()
{
    if (inbound.IsConnected())
        inbound.Close();

    inbound.Destroy();
}

void
LbTcpConnection::DestroyOutbound()
{
    if (outbound.IsConnected())
        outbound.Close();

    outbound.Destroy();
}

void
LbTcpConnection::Destroy()
{
    if (inbound.IsValid())
        DestroyInbound();

    if (cancel_connect)
        cancel_connect.Cancel();
    else if (outbound.IsValid())
        DestroyOutbound();
}

/*
 * inbound BufferedSocketHandler
 *
 */

static BufferedResult
inbound_buffered_socket_data(const void *buffer, size_t size, void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->got_inbound_data = true;

    if (tcp->cancel_connect)
        /* outbound is not yet connected */
        return BufferedResult::BLOCKING;

    if (!tcp->outbound.IsValid()) {
        tcp->Destroy();
        tcp->handler.OnTcpError("Send error", "Broken socket");
        return BufferedResult::CLOSED;
    }

    ssize_t nbytes = tcp->outbound.Write(buffer, size);
    if (nbytes > 0) {
        tcp->inbound.Consumed(nbytes);
        return (size_t)nbytes == size
            ? BufferedResult::OK
            : BufferedResult::PARTIAL;
    }

    switch ((enum write_result)nbytes) {
    case WRITE_SOURCE_EOF:
        assert(false);
        gcc_unreachable();

    case WRITE_ERRNO:
        tcp->Destroy();
        tcp->handler.OnTcpErrno("Send failed", errno);
        return BufferedResult::CLOSED;

    case WRITE_BLOCKING:
        return BufferedResult::BLOCKING;

    case WRITE_DESTROYED:
        return BufferedResult::CLOSED;

    case WRITE_BROKEN:
        tcp->Destroy();
        tcp->handler.OnTcpEnd();
        return BufferedResult::CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

static bool
inbound_buffered_socket_closed(void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->Destroy();
    tcp->handler.OnTcpEnd();
    return false;
}

static bool
inbound_buffered_socket_write(void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->got_outbound_data = false;

    if (!tcp->outbound.Read(false))
        return false;

    if (!tcp->got_outbound_data)
        tcp->inbound.UnscheduleWrite();
    return true;
}

static bool
inbound_buffered_socket_drained(void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    if (!tcp->outbound.IsValid()) {
        /* now that inbound's output buffers are drained, we can
           finally close the connection (postponed from
           outbound_buffered_socket_end()) */
        tcp->Destroy();
        tcp->handler.OnTcpEnd();
        return false;
    }

    return true;
}

static enum write_result
inbound_buffered_socket_broken(void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->Destroy();
    tcp->handler.OnTcpEnd();
    return WRITE_DESTROYED;
}

static void
inbound_buffered_socket_error(std::exception_ptr ep, void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->Destroy();
    tcp->handler.OnTcpError("Error", ep);
}

static constexpr BufferedSocketHandler inbound_buffered_socket_handler = {
    inbound_buffered_socket_data,
    nullptr, // TODO: inbound_buffered_socket_direct,
    inbound_buffered_socket_closed,
    nullptr,
    nullptr,
    inbound_buffered_socket_write,
    inbound_buffered_socket_drained,
    nullptr,
    inbound_buffered_socket_broken,
    inbound_buffered_socket_error,
};

/*
 * outbound buffered_socket_handler
 *
 */

static BufferedResult
outbound_buffered_socket_data(const void *buffer, size_t size, void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->got_outbound_data = true;

    ssize_t nbytes = tcp->inbound.Write(buffer, size);
    if (nbytes > 0) {
        tcp->outbound.Consumed(nbytes);
        return (size_t)nbytes == size
            ? BufferedResult::OK
            : BufferedResult::PARTIAL;
    }

    switch ((enum write_result)nbytes) {
        int save_errno;

    case WRITE_SOURCE_EOF:
        assert(false);
        gcc_unreachable();

    case WRITE_ERRNO:
        save_errno = errno;
        tcp->Destroy();
        tcp->handler.OnTcpErrno("Send failed", save_errno);
        return BufferedResult::CLOSED;

    case WRITE_BLOCKING:
        return BufferedResult::BLOCKING;

    case WRITE_DESTROYED:
        return BufferedResult::CLOSED;

    case WRITE_BROKEN:
        tcp->Destroy();
        tcp->handler.OnTcpEnd();
        return BufferedResult::CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

static bool
outbound_buffered_socket_closed(void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->outbound.Close();
    return true;
}

static void
outbound_buffered_socket_end(void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->outbound.Destroy();

    tcp->inbound.UnscheduleWrite();

    if (tcp->inbound.IsDrained()) {
        /* all output buffers to "inbound" are drained; close the
           connection, because there's nothing left to do */
        tcp->Destroy();
        tcp->handler.OnTcpEnd();

        /* nothing will be done if the buffers are not yet drained;
           we're waiting for inbound_buffered_socket_drained() to be
           called */
    }
}

static bool
outbound_buffered_socket_write(void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->got_inbound_data = false;

    if (!tcp->inbound.Read(false))
        return false;

    if (!tcp->got_inbound_data)
        tcp->outbound.UnscheduleWrite();
    return true;
}

static enum write_result
outbound_buffered_socket_broken(void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->Destroy();
    tcp->handler.OnTcpEnd();
    return WRITE_DESTROYED;
}

static void
outbound_buffered_socket_error(std::exception_ptr ep, void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    tcp->Destroy();
    tcp->handler.OnTcpError("Error", ep);
}

static constexpr BufferedSocketHandler outbound_buffered_socket_handler = {
    outbound_buffered_socket_data,
    nullptr, // TODO: outbound_buffered_socket_direct,
    outbound_buffered_socket_closed,
    nullptr,
    outbound_buffered_socket_end,
    outbound_buffered_socket_write,
    nullptr,
    nullptr,
    outbound_buffered_socket_broken,
    outbound_buffered_socket_error,
};

/*
 * ConnectSocketHandler
 *
 */

void
LbTcpConnection::OnSocketConnectSuccess(UniqueSocketDescriptor &&fd)
{
    cancel_connect = nullptr;

    outbound.Init(fd.Release(), FdType::FD_TCP,
                  nullptr, &write_timeout,
                  outbound_buffered_socket_handler, this);

    /* TODO
    outbound.direct = pipe_stock != nullptr &&
        (ISTREAM_TO_TCP & FdType::FD_PIPE) != 0 &&
        (istream_direct_mask_to(inbound.base.base.fd_type) & FdType::FD_PIPE) != 0;
    */

    if (inbound.Read(false))
        outbound.Read(false);
}

void
LbTcpConnection::OnSocketConnectTimeout()
{
    cancel_connect = nullptr;

    DestroyInbound();
    handler.OnTcpError("Connect error", "Timeout");
}

void
LbTcpConnection::OnSocketConnectError(std::exception_ptr ep)
{
    cancel_connect = nullptr;

    DestroyInbound();
    handler.OnTcpError("Connect error", ep);
}

/*
 * constructor
 *
 */

gcc_pure
static sticky_hash_t
lb_tcp_sticky(StickyMode sticky_mode,
              SocketAddress remote_address)
{
    switch (sticky_mode) {
    case StickyMode::NONE:
    case StickyMode::FAILOVER:
        break;

    case StickyMode::SOURCE_IP:
        return socket_address_sticky(remote_address);

    case StickyMode::SESSION_MODULO:
    case StickyMode::COOKIE:
    case StickyMode::JVM_ROUTE:
        /* not implemented here */
        break;
    }

    return 0;
}

LbTcpConnection::LbTcpConnection(struct pool &_pool, EventLoop &event_loop,
                                 Stock *_pipe_stock,
                                 UniqueSocketDescriptor &&fd, FdType fd_type,
                                 const SocketFilter *filter, void *filter_ctx,
                                 SocketAddress remote_address,
                                 const LbClusterConfig &_cluster,
                                 LbClusterMap &_clusters,
                                 Balancer &_balancer,
                                 LbTcpConnectionHandler &_handler)
    :pool(_pool), pipe_stock(_pipe_stock),
     handler(_handler),
     inbound(event_loop), outbound(event_loop),
     cluster(_cluster), clusters(_clusters), balancer(_balancer),
     session_sticky(lb_tcp_sticky(cluster.sticky_mode, remote_address))
{
    inbound.Init(fd.Release(), fd_type,
                 nullptr, &write_timeout,
                 filter, filter_ctx,
                 inbound_buffered_socket_handler, this);
    /* TODO
    inbound.base.direct = pipe_stock != nullptr &&
        (ISTREAM_TO_PIPE & fd_type) != 0 &&
        (ISTREAM_TO_TCP & FdType::FD_PIPE) != 0;
    */

    if (cluster.transparent_source) {
        bind_address = remote_address;
        bind_address.SetPort(0);
    } else
        bind_address.Clear();
}

void
LbTcpConnection::ConnectOutbound()
{
    if (cluster.HasZeroConf()) {
        auto *cluster2 = clusters.Find(cluster.name);
        if (cluster2 == nullptr) {
            DestroyInbound();
            handler.OnTcpError("Zeroconf error", "Zeroconf cluster not found");
            return;
        }

        const auto member = cluster2->Pick(session_sticky);
        if (member.first == nullptr) {
            DestroyInbound();
            handler.OnTcpError("Zeroconf error", "Zeroconf cluster is empty");
            return;
        }

        const auto address = member.second;
        assert(address.IsDefined());

        client_socket_new(inbound.GetEventLoop(), pool,
                          address.GetFamily(), SOCK_STREAM, 0,
                          cluster.transparent_source, bind_address,
                          address,
                          20,
                          *this,
                          cancel_connect);
        return;
    }

    client_balancer_connect(inbound.GetEventLoop(), pool, balancer,
                            cluster.transparent_source,
                            bind_address,
                            session_sticky,
                            &cluster.address_list,
                            20,
                            *this,
                            cancel_connect);
}

void
LbTcpConnection::OnHandshake()
{
    assert(!cancel_connect);
    assert(!outbound.IsValid());

    ConnectOutbound();
}
