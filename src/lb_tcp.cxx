/*
 * Handler for raw TCP connections.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_tcp.hxx"
#include "lb_config.hxx"
#include "lb_cluster.hxx"
#include "filtered_socket.hxx"
#include "client_balancer.hxx"
#include "address_sticky.hxx"
#include "direct.hxx"
#include "pool.hxx"
#include "GException.hxx"
#include "net/ConnectSocket.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"
#include "util/Cancellable.hxx"

#include <unistd.h>
#include <errno.h>

struct LbTcpConnection final : ConnectSocketHandler {
    struct pool *pool;
    Stock *pipe_stock;

    const LbTcpConnectionHandler *handler;
    void *handler_ctx;

    FilteredSocket inbound;

    BufferedSocket outbound;

    CancellablePointer cancel_connect;

    bool got_inbound_data, got_outbound_data;

    LbTcpConnection(struct pool &_pool, EventLoop &event_loop,
                    Stock *_pipe_stock,
                    SocketDescriptor &&fd, FdType fd_type,
                    const SocketFilter *filter, void *filter_ctx,
                    const LbTcpConnectionHandler &_handler, void *ctx);

    void DestroyInbound();
    void DestroyOutbound();

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(SocketDescriptor &&fd) override;
    void OnSocketConnectTimeout() override;
    void OnSocketConnectError(GError *error) override;
};

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
        lb_tcp_close(tcp);
        tcp->handler->error("Send error", "Broken socket", tcp->handler_ctx);
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
        lb_tcp_close(tcp);
        tcp->handler->_errno("Send failed", errno, tcp->handler_ctx);
        return BufferedResult::CLOSED;

    case WRITE_BLOCKING:
        return BufferedResult::BLOCKING;

    case WRITE_DESTROYED:
        return BufferedResult::CLOSED;

    case WRITE_BROKEN:
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);
        return BufferedResult::CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

static bool
inbound_buffered_socket_closed(void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->eof(tcp->handler_ctx);
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
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);
        return false;
    }

    return true;
}

static enum write_result
inbound_buffered_socket_broken(void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->eof(tcp->handler_ctx);
    return WRITE_DESTROYED;
}

static void
inbound_buffered_socket_error(std::exception_ptr ep, void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->gerror("Error", ToGError(ep), tcp->handler_ctx);
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
        lb_tcp_close(tcp);
        tcp->handler->_errno("Send failed", save_errno, tcp->handler_ctx);
        return BufferedResult::CLOSED;

    case WRITE_BLOCKING:
        return BufferedResult::BLOCKING;

    case WRITE_DESTROYED:
        return BufferedResult::CLOSED;

    case WRITE_BROKEN:
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);
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
        lb_tcp_close(tcp);
        tcp->handler->eof(tcp->handler_ctx);

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

    lb_tcp_close(tcp);
    tcp->handler->eof(tcp->handler_ctx);
    return WRITE_DESTROYED;
}

static void
outbound_buffered_socket_error(std::exception_ptr ep, void *ctx)
{
    LbTcpConnection *tcp = (LbTcpConnection *)ctx;

    lb_tcp_close(tcp);
    tcp->handler->gerror("Error", ToGError(ep), tcp->handler_ctx);
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
LbTcpConnection::OnSocketConnectSuccess(SocketDescriptor &&fd)
{
    cancel_connect = nullptr;

    outbound.Init(fd.Steal(), FdType::FD_TCP,
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
    DestroyInbound();
    handler->error("Connect error", "Timeout", handler_ctx);
}

void
LbTcpConnection::OnSocketConnectError(GError *error)
{
    DestroyInbound();
    handler->gerror("Connect error", error, handler_ctx);
}

/*
 * constructor
 *
 */

gcc_pure
static unsigned
lb_tcp_sticky(const AddressList &address_list,
              SocketAddress remote_address)
{
    switch (address_list.sticky_mode) {
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

inline
LbTcpConnection::LbTcpConnection(struct pool &_pool, EventLoop &event_loop,
                                 Stock *_pipe_stock,
                                 SocketDescriptor &&fd, FdType fd_type,
                                 const SocketFilter *filter, void *filter_ctx,
                                 const LbTcpConnectionHandler &_handler, void *ctx)
    :pool(&_pool), pipe_stock(_pipe_stock),
     handler(&_handler), handler_ctx(ctx),
     inbound(event_loop), outbound(event_loop)
{
    inbound.Init(fd.Steal(), fd_type,
                 nullptr, &write_timeout,
                 filter, filter_ctx,
                 inbound_buffered_socket_handler, this);
    /* TODO
    inbound.base.direct = pipe_stock != nullptr &&
        (ISTREAM_TO_PIPE & fd_type) != 0 &&
        (ISTREAM_TO_TCP & FdType::FD_PIPE) != 0;
    */
}

void
lb_tcp_new(struct pool &pool, EventLoop &event_loop, Stock *pipe_stock,
           SocketDescriptor &&fd, FdType fd_type,
           const SocketFilter *filter, void *filter_ctx,
           SocketAddress remote_address,
           const LbClusterConfig &cluster,
           LbClusterMap &clusters,
           Balancer &balancer,
           const LbTcpConnectionHandler &handler, void *ctx,
           LbTcpConnection **tcp_r)
{
    auto *tcp = NewFromPool<LbTcpConnection>(pool, pool, event_loop,
                                             pipe_stock,
                                             std::move(fd), fd_type,
                                             filter, filter_ctx,
                                             handler, ctx);

    unsigned session_sticky = lb_tcp_sticky(cluster.address_list,
                                            remote_address);

    SocketAddress bind_address = SocketAddress::Null();
    StaticSocketAddress bind_address_buffer;

    if (cluster.transparent_source) {
        bind_address = remote_address;

        /* reset the port to 0 to allow the kernel to choose one */
        if (bind_address.GetPort() != 0) {
            bind_address_buffer = bind_address;
            if (bind_address_buffer.SetPort(0))
                bind_address = bind_address_buffer;
        }
    }

    *tcp_r = tcp;

    if (cluster.HasZeroConf()) {
        /* TODO: generalize the Zeroconf code, implement sticky */

        auto *cluster2 = clusters.Find(cluster.name);
        if (cluster2 == nullptr) {
            tcp->DestroyInbound();
            handler.error("Zeroconf error", "Zeroconf cluster not found", ctx);
            return;
        }

        const auto member = cluster2->Pick();
        if (member.first == nullptr) {
            tcp->DestroyInbound();
            handler.error("Zeroconf error", "Zeroconf cluster is empty", ctx);
            return;
        }

        const auto address = member.second;
        assert(address.IsDefined());

        client_socket_new(event_loop, pool,
                          address.GetFamily(), SOCK_STREAM, 0,
                          cluster.transparent_source, bind_address,
                          address,
                          20,
                          *tcp,
                          tcp->cancel_connect);
        return;
    }

    client_balancer_connect(event_loop, pool, balancer,
                            cluster.transparent_source,
                            bind_address,
                            session_sticky,
                            &cluster.address_list,
                            20,
                            *tcp,
                            tcp->cancel_connect);
}

void
lb_tcp_close(LbTcpConnection *tcp)
{
    if (tcp->inbound.IsValid())
        tcp->DestroyInbound();

    if (tcp->cancel_connect)
        tcp->cancel_connect.Cancel();
    else if (tcp->outbound.IsValid())
        tcp->DestroyOutbound();
}
