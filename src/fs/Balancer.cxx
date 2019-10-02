/*
 * Copyright 2007-2018 Content Management AG
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

#include "Balancer.hxx"
#include "Stock.hxx"
#include "cluster/BalancerRequest.hxx"
#include "stock/Stock.hxx"
#include "stock/GetHandler.hxx"
#include "event/Loop.hxx"
#include "stopwatch.hxx"

class FilteredSocketBalancerRequest : public StockGetHandler {
    FilteredSocketStock &stock;

    const StopwatchPtr &parent_stopwatch;

    const bool ip_transparent;
    const SocketAddress bind_address;

    const Event::Duration timeout;

    SocketFilterFactory *const filter_factory;

    StockGetHandler &handler;

public:
    FilteredSocketBalancerRequest(FilteredSocketStock &_stock,
                                  const StopwatchPtr &_parent_stopwatch,
                                  bool _ip_transparent,
                                  SocketAddress _bind_address,
                                  Event::Duration _timeout,
                                  SocketFilterFactory *_filter_factory,
                                  StockGetHandler &_handler) noexcept
        :stock(_stock),
         parent_stopwatch(_parent_stopwatch),
         ip_transparent(_ip_transparent),
         bind_address(_bind_address),
         timeout(_timeout),
         filter_factory(_filter_factory),
         handler(_handler) {}

    void Send(struct pool &pool, SocketAddress address,
              CancellablePointer &cancel_ptr) noexcept;

private:
    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) noexcept final;
    void OnStockItemError(std::exception_ptr ep) noexcept override;
};

using BR = BalancerRequest<FilteredSocketBalancerRequest>;

inline void
FilteredSocketBalancerRequest::Send(struct pool &pool, SocketAddress address,
                                    CancellablePointer &cancel_ptr) noexcept
{
    stock.Get(pool,
              StopwatchPtr(parent_stopwatch, "connect"),
              nullptr,
              ip_transparent, bind_address, address,
              timeout,
              filter_factory,
              *this,
              cancel_ptr);
}

/*
 * stock handler
 *
 */

void
FilteredSocketBalancerRequest::OnStockItemReady(StockItem &item) noexcept
{
    auto &base = BR::Cast(*this);
    base.ConnectSuccess();

    handler.OnStockItemReady(item);
    base.Destroy();
}

void
FilteredSocketBalancerRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
    auto &base = BR::Cast(*this);
    if (!base.ConnectFailure(stock.GetEventLoop().SteadyNow())) {
        auto &_handler = handler;
        base.Destroy();
        _handler.OnStockItemError(ep);
    }
}

/*
 * public API
 *
 */

EventLoop &
FilteredSocketBalancer::GetEventLoop() noexcept
{
    return stock.GetEventLoop();
}

void
FilteredSocketBalancer::Get(struct pool &pool,
                            const StopwatchPtr &parent_stopwatch,
                            bool ip_transparent,
                            SocketAddress bind_address,
                            sticky_hash_t session_sticky,
                            const AddressList &address_list,
                            Event::Duration timeout,
                            SocketFilterFactory *filter_factory,
                            StockGetHandler &handler,
                            CancellablePointer &cancel_ptr) noexcept
{
    BR::Start(pool, GetEventLoop().SteadyNow(), balancer,
              address_list, cancel_ptr,
              session_sticky,
              stock, parent_stopwatch,
              ip_transparent,
              bind_address, timeout,
              filter_factory,
              handler);
}
