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

#include "ConnectBalancer.hxx"
#include "BalancerRequest.hxx"
#include "net/PConnectSocket.hxx"
#include "address_list.hxx"
#include "event/Loop.hxx"
#include "net/StaticSocketAddress.hxx"
#include "stopwatch.hxx"

struct ClientBalancerRequest : ConnectSocketHandler {
    EventLoop &event_loop;

    bool ip_transparent;
    StaticSocketAddress bind_address;

    /**
     * The connect timeout for each attempt.
     */
    const Event::Duration timeout;

    ConnectSocketHandler &handler;

    ClientBalancerRequest(EventLoop &_event_loop,
                          bool _ip_transparent, SocketAddress _bind_address,
                          Event::Duration _timeout,
                          ConnectSocketHandler &_handler)
        :event_loop(_event_loop), ip_transparent(_ip_transparent),
         timeout(_timeout),
         handler(_handler) {
        if (_bind_address.IsNull() || !_bind_address.IsDefined())
            bind_address.Clear();
        else
            bind_address = _bind_address;
    }

    void Send(AllocatorPtr alloc, SocketAddress address,
              CancellablePointer &cancel_ptr);

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept override;
    void OnSocketConnectTimeout() noexcept override;
    void OnSocketConnectError(std::exception_ptr ep) noexcept override;
};

inline void
ClientBalancerRequest::Send(AllocatorPtr alloc, SocketAddress address,
                            CancellablePointer &cancel_ptr)
{
    client_socket_new(event_loop, alloc, nullptr,
                      address.GetFamily(), SOCK_STREAM, 0,
                      ip_transparent,
                      bind_address,
                      address,
                      timeout,
                      *this,
                      cancel_ptr);
}

/*
 * client_socket_handler
 *
 */

void
ClientBalancerRequest::OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept
{
    auto &base = BalancerRequest<ClientBalancerRequest>::Cast(*this);
    base.ConnectSuccess();

    auto &_handler = handler;
    base.Destroy();
    _handler.OnSocketConnectSuccess(std::move(fd));
}

void
ClientBalancerRequest::OnSocketConnectTimeout() noexcept
{
    auto &base = BalancerRequest<ClientBalancerRequest>::Cast(*this);
    if (!base.ConnectFailure(event_loop.SteadyNow())) {
        auto &_handler = handler;
        base.Destroy();
        _handler.OnSocketConnectTimeout();
    }
}

void
ClientBalancerRequest::OnSocketConnectError(std::exception_ptr ep) noexcept
{
    auto &base = BalancerRequest<ClientBalancerRequest>::Cast(*this);
    if (!base.ConnectFailure(event_loop.SteadyNow())) {
        auto &_handler = handler;
        base.Destroy();
        _handler.OnSocketConnectError(ep);
    }
}

/*
 * constructor
 *
 */

void
client_balancer_connect(EventLoop &event_loop,
                        struct pool &pool, BalancerMap &balancer,
                        bool ip_transparent,
                        SocketAddress bind_address,
                        sticky_hash_t session_sticky,
                        const AddressList *address_list,
                        Event::Duration timeout,
                        ConnectSocketHandler &handler,
                        CancellablePointer &cancel_ptr)
{
    BalancerRequest<ClientBalancerRequest>::Start(pool, event_loop.SteadyNow(),
                                                  balancer,
                                                  *address_list,
                                                  cancel_ptr,
                                                  session_sticky,
                                                  event_loop,
                                                  ip_transparent,
                                                  bind_address,
                                                  timeout,
                                                  handler);
}
