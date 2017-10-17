/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "TcpConnection.hxx"
#include "ClusterConfig.hxx"
#include "ListenerConfig.hxx"
#include "Instance.hxx"
#include "client_balancer.hxx"
#include "address_sticky.hxx"
#include "ssl/ssl_filter.hxx"
#include "pool.hxx"
#include "thread_socket_filter.hxx"
#include "thread_pool.hxx"
#include "net/SocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "address_string.hxx"

#include <assert.h>

static constexpr timeval write_timeout = { 30, 0 };

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

    case StickyMode::HOST:
    case StickyMode::XHOST:
    case StickyMode::SESSION_MODULO:
    case StickyMode::COOKIE:
    case StickyMode::JVM_ROUTE:
        /* not implemented here */
        break;
    }

    return 0;
}

/*
 * inbound BufferedSocketHandler
 *
 */

static BufferedResult
inbound_buffered_socket_data(const void *buffer, size_t size, void *ctx)
{
    auto *tcp = (LbTcpConnection *)ctx;

    tcp->got_inbound_data = true;

    if (tcp->cancel_connect)
        /* outbound is not yet connected */
        return BufferedResult::BLOCKING;

    if (!tcp->outbound.IsValid()) {
        tcp->DestroyBoth();
        tcp->OnTcpError("Send error", "Broken socket");
        return BufferedResult::CLOSED;
    }

    ssize_t nbytes = tcp->outbound.Write(buffer, size);
    if (nbytes > 0) {
        tcp->outbound.ScheduleWrite();
        tcp->inbound.Consumed(nbytes);
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
        tcp->DestroyBoth();
        tcp->OnTcpErrno("Send failed", save_errno);
        return BufferedResult::CLOSED;

    case WRITE_BLOCKING:
        return BufferedResult::BLOCKING;

    case WRITE_DESTROYED:
        return BufferedResult::CLOSED;

    case WRITE_BROKEN:
        tcp->DestroyBoth();
        tcp->OnTcpEnd();
        return BufferedResult::CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

static bool
inbound_buffered_socket_closed(void *ctx)
{
    auto *tcp = (LbTcpConnection *)ctx;

    tcp->DestroyBoth();
    tcp->OnTcpEnd();
    return false;
}

static bool
inbound_buffered_socket_write(void *ctx)
{
    auto *tcp = (LbTcpConnection *)ctx;

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
    auto *tcp = (LbTcpConnection *)ctx;

    if (!tcp->outbound.IsValid()) {
        /* now that inbound's output buffers are drained, we can
           finally close the connection (postponed from
           outbound_buffered_socket_end()) */
        tcp->DestroyBoth();
        tcp->OnTcpEnd();
        return false;
    }

    return true;
}

static enum write_result
inbound_buffered_socket_broken(void *ctx)
{
    auto *tcp = (LbTcpConnection *)ctx;

    tcp->DestroyBoth();
    tcp->OnTcpEnd();
    return WRITE_DESTROYED;
}

static void
inbound_buffered_socket_error(std::exception_ptr ep, void *ctx)
{
    auto *tcp = (LbTcpConnection *)ctx;

    tcp->DestroyBoth();
    tcp->OnTcpError("Error", ep);
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
    auto *tcp = (LbTcpConnection *)ctx;

    tcp->got_outbound_data = true;

    ssize_t nbytes = tcp->inbound.Write(buffer, size);
    if (nbytes > 0) {
        tcp->inbound.ScheduleWrite();
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
        tcp->DestroyBoth();
        tcp->OnTcpErrno("Send failed", save_errno);
        return BufferedResult::CLOSED;

    case WRITE_BLOCKING:
        return BufferedResult::BLOCKING;

    case WRITE_DESTROYED:
        return BufferedResult::CLOSED;

    case WRITE_BROKEN:
        tcp->DestroyBoth();
        tcp->OnTcpEnd();
        return BufferedResult::CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

static bool
outbound_buffered_socket_closed(void *ctx)
{
    auto *tcp = (LbTcpConnection *)ctx;

    tcp->outbound.Close();
    return true;
}

static void
outbound_buffered_socket_end(void *ctx)
{
    auto *tcp = (LbTcpConnection *)ctx;

    tcp->outbound.Destroy();

    tcp->inbound.UnscheduleWrite();

    if (tcp->inbound.IsDrained()) {
        /* all output buffers to "inbound" are drained; close the
           connection, because there's nothing left to do */
        tcp->DestroyBoth();
        tcp->OnTcpEnd();

        /* nothing will be done if the buffers are not yet drained;
           we're waiting for inbound_buffered_socket_drained() to be
           called */
    }
}

static bool
outbound_buffered_socket_write(void *ctx)
{
    auto *tcp = (LbTcpConnection *)ctx;

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
    auto *tcp = (LbTcpConnection *)ctx;

    tcp->DestroyBoth();
    tcp->OnTcpEnd();
    return WRITE_DESTROYED;
}

static void
outbound_buffered_socket_error(std::exception_ptr ep, void *ctx)
{
    auto *tcp = (LbTcpConnection *)ctx;

    tcp->DestroyBoth();
    tcp->OnTcpError("Error", ep);
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

std::string
LbTcpConnection::MakeLoggerDomain() const noexcept
{
    return "listener='" + listener.name
        + "' cluster='" + listener.destination.GetName()
        + "' client='" + client_address
        + "'";
}

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
LbTcpConnection::DestroyBoth()
{
    if (inbound.IsValid())
        DestroyInbound();

    if (cancel_connect)
        cancel_connect.Cancel();
    else if (outbound.IsValid())
        DestroyOutbound();
}

void
LbTcpConnection::OnHandshake()
{
    assert(!cancel_connect);
    assert(!outbound.IsValid());

    ConnectOutbound();
}

void
LbTcpConnection::OnTcpEnd()
{
    Destroy();
}

void
LbTcpConnection::OnTcpError(const char *prefix, const char *error)
{
    logger(3, prefix, ": ", error);
    Destroy();
}

void
LbTcpConnection::OnTcpErrno(const char *prefix, int error)
{
    logger(3, prefix, ": ", strerror(error));
    Destroy();
}

void
LbTcpConnection::OnTcpError(const char *prefix, std::exception_ptr ep)
{
    logger(3, prefix, ": ", ep);
    Destroy();
}

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
    OnTcpError("Connect error", "Timeout");
}

void
LbTcpConnection::OnSocketConnectError(std::exception_ptr ep)
{
    cancel_connect = nullptr;

    DestroyInbound();
    OnTcpError("Connect error", ep);
}

void
LbTcpConnection::ConnectOutbound()
{
    const auto &cluster_config = cluster.GetConfig();

    if (cluster_config.HasZeroConf()) {
        const auto member = cluster.Pick(session_sticky);
        if (member.first == nullptr) {
            DestroyInbound();
            OnTcpError("Zeroconf error", "Zeroconf cluster is empty");
            return;
        }

        const auto address = member.second;
        assert(address.IsDefined());

        client_socket_new(inbound.GetEventLoop(), pool,
                          address.GetFamily(), SOCK_STREAM, 0,
                          cluster_config.transparent_source, bind_address,
                          address,
                          20,
                          *this,
                          cancel_connect);
        return;
    }

    client_balancer_connect(inbound.GetEventLoop(), pool, *instance.balancer,
                            cluster_config.transparent_source,
                            bind_address,
                            session_sticky,
                            &cluster_config.address_list,
                            20,
                            *this,
                            cancel_connect);
}

/*
 * public
 *
 */

LbTcpConnection::LbTcpConnection(struct pool &_pool, LbInstance &_instance,
                                 const LbListenerConfig &_listener,
                                 LbCluster &_cluster,
                                 UniqueSocketDescriptor &&fd, FdType fd_type,
                                 const SocketFilter *filter, void *filter_ctx,
                                 SocketAddress _client_address)
    :pool(_pool), instance(_instance), listener(_listener), cluster(_cluster),
     client_address(address_to_string(pool, _client_address)),
     session_sticky(lb_tcp_sticky(cluster.GetConfig().sticky_mode,
                                  _client_address)),
     logger(*this),
     inbound(instance.event_loop), outbound(instance.event_loop)
{
    if (client_address == nullptr)
        client_address = "unknown";

    inbound.Init(fd.Release(), fd_type,
                 nullptr, &write_timeout,
                 filter, filter_ctx,
                 inbound_buffered_socket_handler, this);
    /* TODO
       inbound.base.direct = pipe_stock != nullptr &&
       (ISTREAM_TO_PIPE & fd_type) != 0 &&
       (ISTREAM_TO_TCP & FdType::FD_PIPE) != 0;
    */

    if (cluster.GetConfig().transparent_source) {
        bind_address = _client_address;
        bind_address.SetPort(0);
    } else
        bind_address.Clear();

    ScheduleHandshakeCallback();

    instance.tcp_connections.push_back(*this);
}

LbTcpConnection::~LbTcpConnection()
{
    DestroyBoth();

    auto &connections = instance.tcp_connections;
    connections.erase(connections.iterator_to(*this));
}

LbTcpConnection *
LbTcpConnection::New(LbInstance &instance,
                     const LbListenerConfig &listener,
                     LbCluster &cluster,
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

    return NewFromPool<LbTcpConnection>(*pool, *pool, instance,
                                        listener, cluster,
                                        std::move(fd), fd_type,
                                        filter, filter_ctx,
                                        address);
}

void
LbTcpConnection::Destroy()
{
    assert(!instance.tcp_connections.empty());
    assert(listener.destination.GetProtocol() == LbProtocol::TCP);

    DeleteUnrefTrashPool(pool, this);
}
