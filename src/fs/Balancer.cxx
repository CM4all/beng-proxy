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

#include "Balancer.hxx"
#include "Handler.hxx"
#include "Stock.hxx"
#include "cluster/BalancerRequest.hxx"
#include "cluster/AddressListWrapper.hxx"
#include "cluster/AddressList.hxx"
#include "stock/GetHandler.hxx"
#include "event/Loop.hxx"
#include "stopwatch.hxx"
#include "lease.hxx"

class FilteredSocketBalancer::Request : public StockGetHandler, Lease {
	FilteredSocketStock &stock;

	const StopwatchPtr parent_stopwatch;

	const bool ip_transparent;
	const SocketAddress bind_address;

	const Event::Duration timeout;

	SocketFilterFactory *const filter_factory;

	FilteredSocketBalancerHandler &handler;

	StockItem *stock_item;

public:
	Request(FilteredSocketStock &_stock,
		const StopwatchPtr &_parent_stopwatch,
		bool _ip_transparent,
		SocketAddress _bind_address,
		Event::Duration _timeout,
		SocketFilterFactory *_filter_factory,
		FilteredSocketBalancerHandler &_handler) noexcept
		:stock(_stock),
		 parent_stopwatch(_parent_stopwatch),
		 ip_transparent(_ip_transparent),
		 bind_address(_bind_address),
		 timeout(_timeout),
		 filter_factory(_filter_factory),
		 handler(_handler) {}

	void Send(AllocatorPtr alloc, SocketAddress address,
		  CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept final;
	void OnStockItemError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Lease */
	void ReleaseLease(bool reuse) noexcept final;
};

using BR = BalancerRequest<FilteredSocketBalancer::Request,
			   BalancerMap::Wrapper<AddressListWrapper>>;

inline void
FilteredSocketBalancer::Request::Send(AllocatorPtr alloc, SocketAddress address,
				      CancellablePointer &cancel_ptr) noexcept
{
	stock.Get(alloc,
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
FilteredSocketBalancer::Request::OnStockItemReady(StockItem &item) noexcept
{
	auto &base = BR::Cast(*this);
	base.ConnectSuccess();

	stock_item = &item;

	handler.OnFilteredSocketReady(*this, fs_stock_item_get(item),
				      fs_stock_item_get_address(item),
				      item.GetStockName(),
				      base.GetFailureInfo());
}

void
FilteredSocketBalancer::Request::OnStockItemError(std::exception_ptr ep) noexcept
{
	auto &base = BR::Cast(*this);
	if (!base.ConnectFailure(stock.GetEventLoop().SteadyNow())) {
		auto &_handler = handler;
		base.Destroy();
		_handler.OnFilteredSocketError(std::move(ep));
	}
}

void
FilteredSocketBalancer::Request::ReleaseLease(bool reuse) noexcept
{
	stock_item->Put(!reuse);

	auto &base = BR::Cast(*this);
	base.Destroy();
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
FilteredSocketBalancer::Get(AllocatorPtr alloc,
			    const StopwatchPtr &parent_stopwatch,
			    bool ip_transparent,
			    SocketAddress bind_address,
			    sticky_hash_t sticky_hash,
			    const AddressList &address_list,
			    Event::Duration timeout,
			    SocketFilterFactory *filter_factory,
			    FilteredSocketBalancerHandler &handler,
			    CancellablePointer &cancel_ptr) noexcept
{
	BR::Start(alloc, GetEventLoop().SteadyNow(),
		  balancer.MakeAddressListWrapper(AddressListWrapper(GetFailureManager(),
								     address_list.addresses),
						  address_list.sticky_mode),
		  cancel_ptr,
		  sticky_hash,
		  stock, parent_stopwatch,
		  ip_transparent,
		  bind_address, timeout,
		  filter_factory,
		  handler);
}
