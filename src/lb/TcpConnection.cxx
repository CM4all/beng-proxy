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
#include "Cluster.hxx"
#include "ClusterConfig.hxx"
#include "ListenerConfig.hxx"
#include "Instance.hxx"
#include "AllocatorPtr.hxx"
#include "cluster/ConnectBalancer.hxx"
#include "address_sticky.hxx"
#include "ssl/Filter.hxx"
#include "fs/ThreadSocketFilter.hxx"
#include "thread_pool.hxx"
#include "net/SocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "address_string.hxx"

#include <assert.h>

static constexpr Event::Duration LB_TCP_CONNECT_TIMEOUT =
    std::chrono::seconds(20);

static constexpr auto write_timeout = std::chrono::seconds(30);

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

BufferedResult
LbTcpConnection::Inbound::OnBufferedData()
{
    auto &tcp = LbTcpConnection::FromInbound(*this);

    tcp.got_inbound_data = true;

    if (tcp.cancel_connect)
        /* outbound is not yet connected */
        return BufferedResult::BLOCKING;

    if (!tcp.outbound.socket.IsValid()) {
        tcp.OnTcpError("Send error", "Broken socket");
        return BufferedResult::CLOSED;
    }

    auto r = socket.ReadBuffer();
    assert(!r.empty());

    ssize_t nbytes = tcp.outbound.socket.Write(r.data, r.size);
    if (nbytes > 0) {
        tcp.outbound.socket.ScheduleWrite();
        socket.DisposeConsumed(nbytes);
        return BufferedResult::OK;
    }

    switch ((enum write_result)nbytes) {
        int save_errno;

    case WRITE_SOURCE_EOF:
        assert(false);
        gcc_unreachable();

    case WRITE_ERRNO:
        save_errno = errno;
        tcp.OnTcpErrno("Send failed", save_errno);
        return BufferedResult::CLOSED;

    case WRITE_BLOCKING:
        return BufferedResult::BLOCKING;

    case WRITE_DESTROYED:
        return BufferedResult::CLOSED;

    case WRITE_BROKEN:
        tcp.OnTcpEnd();
        return BufferedResult::CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

bool
LbTcpConnection::Inbound::OnBufferedClosed() noexcept
{
    auto &tcp = LbTcpConnection::FromInbound(*this);

    tcp.OnTcpEnd();
    return false;
}

bool
LbTcpConnection::Inbound::OnBufferedWrite()
{
    auto &tcp = LbTcpConnection::FromInbound(*this);

    tcp.got_outbound_data = false;

    if (!tcp.outbound.socket.Read(false))
        return false;

    if (!tcp.got_outbound_data)
        socket.UnscheduleWrite();
    return true;
}

bool
LbTcpConnection::Inbound::OnBufferedDrained() noexcept
{
    auto &tcp = LbTcpConnection::FromInbound(*this);

    if (!tcp.outbound.socket.IsValid()) {
        /* now that inbound's output buffers are drained, we can
           finally close the connection (postponed from
           outbound_buffered_socket_end()) */
        tcp.OnTcpEnd();
        return false;
    }

    return true;
}

enum write_result
LbTcpConnection::Inbound::OnBufferedBroken() noexcept
{
    auto &tcp = LbTcpConnection::FromInbound(*this);

    tcp.OnTcpEnd();
    return WRITE_DESTROYED;
}

void
LbTcpConnection::Inbound::OnBufferedError(std::exception_ptr ep) noexcept
{
    auto &tcp = LbTcpConnection::FromInbound(*this);

    tcp.OnTcpError("Error", ep);
}

/*
 * outbound buffered_socket_handler
 *
 */

BufferedResult
LbTcpConnection::Outbound::OnBufferedData()
{
    auto &tcp = LbTcpConnection::FromOutbound(*this);

    tcp.got_outbound_data = true;

    auto r = socket.ReadBuffer();
    assert(!r.empty());

    ssize_t nbytes = tcp.inbound.socket.Write(r.data, r.size);
    if (nbytes > 0) {
        tcp.inbound.socket.ScheduleWrite();
        socket.DisposeConsumed(nbytes);
        return BufferedResult::OK;
    }

    switch ((enum write_result)nbytes) {
        int save_errno;

    case WRITE_SOURCE_EOF:
        assert(false);
        gcc_unreachable();

    case WRITE_ERRNO:
        save_errno = errno;
        tcp.OnTcpErrno("Send failed", save_errno);
        return BufferedResult::CLOSED;

    case WRITE_BLOCKING:
        return BufferedResult::BLOCKING;

    case WRITE_DESTROYED:
        return BufferedResult::CLOSED;

    case WRITE_BROKEN:
        tcp.OnTcpEnd();
        return BufferedResult::CLOSED;
    }

    assert(false);
    gcc_unreachable();
}

bool
LbTcpConnection::Outbound::OnBufferedClosed() noexcept
{
    socket.Close();
    return true;
}

bool
LbTcpConnection::Outbound::OnBufferedEnd() noexcept
{
    auto &tcp = LbTcpConnection::FromOutbound(*this);

    socket.Destroy();

    tcp.inbound.socket.UnscheduleWrite();

    if (tcp.inbound.socket.IsDrained()) {
        /* all output buffers to "inbound" are drained; close the
           connection, because there's nothing left to do */
        tcp.OnTcpEnd();

        /* nothing will be done if the buffers are not yet drained;
           we're waiting for inbound_buffered_socket_drained() to be
           called */
    }

    return true;
}

bool
LbTcpConnection::Outbound::OnBufferedWrite()
{
    auto &tcp = LbTcpConnection::FromOutbound(*this);

    tcp.got_inbound_data = false;

    if (!tcp.inbound.socket.Read(false))
        return false;

    if (!tcp.got_inbound_data)
        socket.UnscheduleWrite();
    return true;
}

enum write_result
LbTcpConnection::Outbound::OnBufferedBroken() noexcept
{
    auto &tcp = LbTcpConnection::FromOutbound(*this);

    tcp.OnTcpEnd();
    return WRITE_DESTROYED;
}

void
LbTcpConnection::Outbound::OnBufferedError(std::exception_ptr ep) noexcept
{
    auto &tcp = LbTcpConnection::FromOutbound(*this);

    tcp.OnTcpError("Error", ep);
}

std::string
LbTcpConnection::MakeLoggerDomain() const noexcept
{
    return "listener='" + listener.name
        + "' cluster='" + listener.destination.GetName()
        + "' client='" + client_address
        + "'";
}

void
LbTcpConnection::Inbound::Destroy()
{
    if (socket.IsConnected())
        socket.Close();

    socket.Destroy();
}

void
LbTcpConnection::Outbound::Destroy()
{
    if (socket.IsConnected())
        socket.Close();

    socket.Destroy();
}

void
LbTcpConnection::DestroyBoth()
{
    if (inbound.socket.IsValid())
        inbound.Destroy();

    defer_connect.Cancel();

    if (cancel_connect) {
        cancel_connect.Cancel();
        cancel_connect = nullptr;
    } else if (outbound.socket.IsValid())
        outbound.Destroy();
}

inline void
LbTcpConnection::OnDeferredHandshake() noexcept
{
    assert(!cancel_connect);
    assert(!outbound.socket.IsValid());

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
LbTcpConnection::OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept
{
    cancel_connect = nullptr;

    outbound.socket.Init(fd.Release(), FdType::FD_TCP,
                         Event::Duration(-1), write_timeout,
                         outbound);

    /* TODO
    outbound.direct = pipe_stock != nullptr &&
        (ISTREAM_TO_TCP & FdType::FD_PIPE) != 0 &&
        (istream_direct_mask_to(inbound.base.base.fd_type) & FdType::FD_PIPE) != 0;
    */

    if (inbound.socket.Read(false))
        outbound.socket.Read(false);
}

void
LbTcpConnection::OnSocketConnectTimeout() noexcept
{
    cancel_connect = nullptr;

    inbound.Destroy();
    OnTcpError("Connect error", "Timeout");
}

void
LbTcpConnection::OnSocketConnectError(std::exception_ptr ep) noexcept
{
    cancel_connect = nullptr;

    inbound.Destroy();
    OnTcpError("Connect error", ep);
}

void
LbTcpConnection::ConnectOutbound()
{
    const auto &cluster_config = cluster.GetConfig();

    if (cluster_config.HasZeroConf()) {
        const auto *member = cluster.Pick(GetEventLoop().SteadyNow(),
                                          session_sticky);
        if (member == nullptr) {
            inbound.Destroy();
            OnTcpError("Zeroconf error", "Zeroconf cluster is empty");
            return;
        }

        const auto address = member->GetAddress();
        assert(address.IsDefined());

        client_socket_new(GetEventLoop(), *pool,
                          address.GetFamily(), SOCK_STREAM, 0,
                          cluster_config.transparent_source, bind_address,
                          address,
                          LB_TCP_CONNECT_TIMEOUT,
                          *this,
                          cancel_connect);
        return;
    }

    client_balancer_connect(GetEventLoop(), pool, *instance.balancer,
                            cluster_config.transparent_source,
                            bind_address,
                            session_sticky,
                            &cluster_config.address_list,
                            LB_TCP_CONNECT_TIMEOUT,
                            *this,
                            cancel_connect);
}

/*
 * public
 *
 */

inline
LbTcpConnection::Inbound::Inbound(EventLoop &event_loop,
                                  UniqueSocketDescriptor &&fd, FdType fd_type,
                                  SocketFilterPtr &&filter)
    :socket(event_loop)
{
    socket.Init(fd.Release(), fd_type,
                Event::Duration(-1), write_timeout,
                std::move(filter),
                *this);
    /* TODO
    socket.base.direct = pipe_stock != nullptr &&
       (ISTREAM_TO_PIPE & fd_type) != 0 &&
       (ISTREAM_TO_TCP & FdType::FD_PIPE) != 0;
    */

}

inline
LbTcpConnection::LbTcpConnection(PoolPtr &&_pool, LbInstance &_instance,
                                 const LbListenerConfig &_listener,
                                 LbCluster &_cluster,
                                 UniqueSocketDescriptor &&fd, FdType fd_type,
                                 SocketFilterPtr &&filter,
                                 SocketAddress _client_address)
    :PoolHolder(std::move(_pool)),
     instance(_instance), listener(_listener), cluster(_cluster),
     client_address(address_to_string(pool, _client_address)),
     session_sticky(lb_tcp_sticky(cluster.GetConfig().sticky_mode,
                                  _client_address)),
     logger(*this),
     inbound(instance.event_loop, std::move(fd), fd_type, std::move(filter)),
     outbound(instance.event_loop),
     defer_connect(instance.event_loop, BIND_THIS_METHOD(OnDeferredHandshake))
{
    if (client_address == nullptr)
        client_address = "unknown";

    if (cluster.GetConfig().transparent_source) {
        bind_address = _client_address;
        bind_address.SetPort(0);
    } else
        bind_address.Clear();

    instance.tcp_connections.push_back(*this);

    ScheduleHandshakeCallback();
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

    SocketFilterPtr filter;

    if (ssl_factory != nullptr) {
        auto *ssl_filter = ssl_filter_new(*ssl_factory);

        filter.reset(new ThreadSocketFilter(instance.event_loop,
                                            thread_pool_get_queue(instance.event_loop),
                                            &ssl_filter_get_handler(*ssl_filter)));
    }

    auto pool = pool_new_linear(instance.root_pool, "client_connection", 2048);
    pool_set_major(pool);

    return NewFromPool<LbTcpConnection>(std::move(pool), instance,
                                        listener, cluster,
                                        std::move(fd), fd_type,
                                        std::move(filter),
                                        address);
}

void
LbTcpConnection::Destroy()
{
    assert(!instance.tcp_connections.empty());
    assert(listener.destination.GetProtocol() == LbProtocol::TCP);

    this->~LbTcpConnection();
}
