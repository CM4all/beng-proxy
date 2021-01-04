/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "TcpBalancer.hxx"
#include "BalancerRequest.hxx"
#include "AddressListWrapper.hxx"
#include "AddressList.hxx"
#include "tcp_stock.hxx"
#include "stock/GetHandler.hxx"
#include "event/Loop.hxx"
#include "stopwatch.hxx"

inline EventLoop &
TcpBalancer::GetEventLoop() noexcept
{
	return tcp_stock.GetEventLoop();
}

class TcpBalancerRequest : StockGetHandler {
	TcpBalancer &tcp_balancer;
	const StopwatchPtr parent_stopwatch;

	const bool ip_transparent;
	const SocketAddress bind_address;

	const Event::Duration timeout;

	StockGetHandler &handler;

public:
	TcpBalancerRequest(TcpBalancer &_tcp_balancer,
			   const StopwatchPtr &_parent_stopwatch,
			   bool _ip_transparent,
			   SocketAddress _bind_address,
			   Event::Duration _timeout,
			   StockGetHandler &_handler) noexcept
		:tcp_balancer(_tcp_balancer), parent_stopwatch(_parent_stopwatch),
		 ip_transparent(_ip_transparent),
		 bind_address(_bind_address),
		 timeout(_timeout),
		 handler(_handler) {}

	EventLoop &GetEventLoop() noexcept {
		return tcp_balancer.GetEventLoop();
	}

	void Send(AllocatorPtr alloc, SocketAddress address,
		  CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr ep) noexcept override;
};

using BR = BalancerRequest<TcpBalancerRequest,
			   BalancerMap::Wrapper<AddressListWrapper>>;

inline void
TcpBalancerRequest::Send(AllocatorPtr alloc, SocketAddress address,
			 CancellablePointer &cancel_ptr) noexcept
{
	tcp_balancer.tcp_stock.Get(alloc,
				   parent_stopwatch,
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
	auto &base = BR::Cast(*this);
	base.ConnectSuccess();

	handler.OnStockItemReady(item);
	base.Destroy();
}

void
TcpBalancerRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	auto &base = BR::Cast(*this);
	if (!base.ConnectFailure(GetEventLoop().SteadyNow())) {
		handler.OnStockItemError(ep);
		base.Destroy();
	}
}

/*
 * public API
 *
 */

void
TcpBalancer::Get(AllocatorPtr alloc, const StopwatchPtr &parent_stopwatch,
		 bool ip_transparent,
		 SocketAddress bind_address,
		 sticky_hash_t sticky_hash,
		 const AddressList &address_list,
		 Event::Duration timeout,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr)
{
	BR::Start(alloc, GetEventLoop().SteadyNow(),
		  balancer.MakeAddressListWrapper(AddressListWrapper(GetFailureManager(),
								     address_list.addresses),
						  address_list.sticky_mode),
		  cancel_ptr,
		  sticky_hash,
		  *this,
		  parent_stopwatch,
		  ip_transparent,
		  bind_address, timeout,
		  handler);
}
