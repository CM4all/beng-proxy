// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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

	uint_least64_t fairness_hash;

	const bool ip_transparent;
	const SocketAddress bind_address;

	const Event::Duration timeout;

	const SocketFilterParams *const filter_params;

	FilteredSocketBalancerHandler &handler;

	StockItem *stock_item;

public:
	Request(FilteredSocketStock &_stock,
		const StopwatchPtr &_parent_stopwatch,
		uint_fast64_t _fairness_hash,
		bool _ip_transparent,
		SocketAddress _bind_address,
		Event::Duration _timeout,
		const SocketFilterParams *_filter_params,
		FilteredSocketBalancerHandler &_handler) noexcept
		:stock(_stock),
		 parent_stopwatch(_parent_stopwatch),
		 fairness_hash(_fairness_hash),
		 ip_transparent(_ip_transparent),
		 bind_address(_bind_address),
		 timeout(_timeout),
		 filter_params(_filter_params),
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
		  nullptr, fairness_hash,
		  ip_transparent, bind_address, address,
		  timeout,
		  filter_params,
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
			    uint_fast64_t fairness_hash,
			    bool ip_transparent,
			    SocketAddress bind_address,
			    sticky_hash_t sticky_hash,
			    const AddressList &address_list,
			    Event::Duration timeout,
			    const SocketFilterParams *filter_params,
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
		  fairness_hash,
		  ip_transparent,
		  bind_address, timeout,
		  filter_params,
		  handler);
}
