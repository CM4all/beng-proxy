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

#include "tcp_balancer.hxx"
#include "tcp_stock.hxx"
#include "generic_balancer.hxx"
#include "stock/GetHandler.hxx"

struct TcpBalancerRequest : public StockGetHandler {
    TcpBalancer &tcp_balancer;

    const bool ip_transparent;
    const SocketAddress bind_address;

    const unsigned timeout;

    StockGetHandler &handler;

    TcpBalancerRequest(TcpBalancer &_tcp_balancer,
                       bool _ip_transparent,
                       SocketAddress _bind_address,
                       unsigned _timeout,
                       StockGetHandler &_handler) noexcept
        :tcp_balancer(_tcp_balancer),
         ip_transparent(_ip_transparent),
         bind_address(_bind_address),
         timeout(_timeout),
         handler(_handler) {}

    void Send(struct pool &pool, SocketAddress address,
              CancellablePointer &cancel_ptr) noexcept;

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) noexcept override;
    void OnStockItemError(std::exception_ptr ep) noexcept override;
};

inline void
TcpBalancerRequest::Send(struct pool &pool, SocketAddress address,
                         CancellablePointer &cancel_ptr) noexcept
{
    tcp_balancer.tcp_stock.Get(pool,
                               nullptr,
                               ip_transparent,
                               bind_address,
                               address,
                               timeout,
                               *this,
                               cancel_ptr);
}

/*
 * stock handler
 *
 */

void
TcpBalancerRequest::OnStockItemReady(StockItem &item) noexcept
{
    auto &base = BalancerRequest<TcpBalancerRequest>::Cast(*this);
    base.ConnectSuccess();

    handler.OnStockItemReady(item);
}

void
TcpBalancerRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
    auto &base = BalancerRequest<TcpBalancerRequest>::Cast(*this);
    if (!base.ConnectFailure())
        handler.OnStockItemError(ep);
}

/*
 * public API
 *
 */

void
TcpBalancer::Get(struct pool &pool,
                 bool ip_transparent,
                 SocketAddress bind_address,
                 sticky_hash_t session_sticky,
                 const AddressList &address_list,
                 unsigned timeout,
                 StockGetHandler &handler,
                 CancellablePointer &cancel_ptr)
{
    BalancerRequest<TcpBalancerRequest>::Start(pool, balancer,
                                               address_list, cancel_ptr,
                                               session_sticky,
                                               *this,
                                               ip_transparent,
                                               bind_address, timeout,
                                               handler);
}
