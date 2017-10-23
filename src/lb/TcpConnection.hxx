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

#ifndef BENG_PROXY_LB_CONNECTION_H
#define BENG_PROXY_LB_CONNECTION_H

#include "filtered_socket.hxx"
#include "StickyHash.hxx"
#include "io/Logger.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/PConnectSocket.hxx"
#include "io/FdType.hxx"
#include "util/Cancellable.hxx"
#include "util/Cast.hxx"

#include <boost/intrusive/list.hpp>

#include <exception>

#include <stdint.h>

struct pool;
struct SslFactory;
struct SslFilter;
struct ThreadSocketFilter;
class UniqueSocketDescriptor;
class SocketAddress;
struct LbListenerConfig;
class LbCluster;
struct LbGoto;
struct LbInstance;

class LbTcpConnection final
    : LoggerDomainFactory, ConnectSocketHandler,
      public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    struct pool &pool;

    LbInstance &instance;

    const LbListenerConfig &listener;
    LbCluster &cluster;

    /**
     * The client's address formatted as a string (for logging).  This
     * is guaranteed to be non-nullptr.
     */
    const char *client_address;

    const sticky_hash_t session_sticky;

    const LazyDomainLogger logger;

public:
    struct Inbound {
        FilteredSocket socket;

        Inbound(EventLoop &event_loop,
                UniqueSocketDescriptor &&fd, FdType fd_type,
                const SocketFilter *filter, void *filter_ctx);

        void Destroy();

        void ScheduleHandshakeCallback(BoundMethod<void()> callback) {
            socket.ScheduleReadNoTimeout(false);
            socket.SetHandshakeCallback(callback);
        }
    } inbound;

    static constexpr LbTcpConnection &FromInbound(Inbound &i) {
        return ContainerCast(i, &LbTcpConnection::inbound);
    }

    struct Outbound {
        BufferedSocket socket;

        explicit Outbound(EventLoop &event_loop)
            :socket(event_loop) {}

        void Destroy();
    } outbound;

    static constexpr LbTcpConnection &FromOutbound(Outbound &o) {
        return ContainerCast(o, &LbTcpConnection::outbound);
    }

    StaticSocketAddress bind_address;
    CancellablePointer cancel_connect;

    bool got_inbound_data, got_outbound_data;

    LbTcpConnection(struct pool &_pool, LbInstance &_instance,
                    const LbListenerConfig &_listener,
                    LbCluster &_cluster,
                    UniqueSocketDescriptor &&fd, FdType fd_type,
                    const SocketFilter *filter, void *filter_ctx,
                    SocketAddress _client_address);

    ~LbTcpConnection();

    static LbTcpConnection *New(LbInstance &instance,
                                const LbListenerConfig &listener,
                                LbCluster &cluster,
                                SslFactory *ssl_factory,
                                UniqueSocketDescriptor &&fd,
                                SocketAddress address);

    EventLoop &GetEventLoop() {
        return inbound.socket.GetEventLoop();
    }

    void Destroy();

protected:
    /* virtual methods from class LoggerDomainFactory */
    std::string MakeLoggerDomain() const noexcept override;

private:
    void ScheduleHandshakeCallback() {
        inbound.ScheduleHandshakeCallback(BIND_THIS_METHOD(OnHandshake));
    }

    void ConnectOutbound();

public:
    void DestroyBoth();

    void OnHandshake();

    void OnTcpEnd();
    void OnTcpError(const char *prefix, const char *error);
    void OnTcpErrno(const char *prefix, int error);
    void OnTcpError(const char *prefix, std::exception_ptr ep);

private:
    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) override;
    void OnSocketConnectTimeout() override;
    void OnSocketConnectError(std::exception_ptr ep) override;
};

#endif
