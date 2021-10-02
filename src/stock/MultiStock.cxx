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

#include "MultiStock.hxx"
#include "stock/MapStock.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "util/djbhash.h"
#include "util/DeleteDisposer.hxx"
#include "util/StringAPI.hxx"

#include <cassert>

void
MultiStock::SharedItem::Lease::ReleaseLease(bool _reuse) noexcept
{
	item.DeleteLease(this, _reuse);
}

MultiStock::SharedItem::~SharedItem() noexcept
{
	assert(leases.empty());

	shared_item.Put(!reuse);
}

void
MultiStock::SharedItem::AddLease(StockGetHandler &handler,
			   LeasePtr &lease_ref) noexcept
{
	lease_ref.Set(AddLease());

	handler.OnStockItemReady(shared_item);
}

inline void
MultiStock::SharedItem::DeleteLease(Lease *lease, bool _reuse) noexcept
{
	reuse &= _reuse;

	assert(!leases.empty());
	leases.erase_and_dispose(leases.iterator_to(*lease),
				 DeleteDisposer());
	++remaining_leases;

	parent.OnLeaseReleased(*this);
}

struct MultiStock::MapItem::Waiting final
	: boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
		  Cancellable
{
	MapItem &parent;
	StockRequest request;
	LeasePtr &lease_ref;
	StockGetHandler &handler;
	CancellablePointer &caller_cancel_ptr;

	Waiting(MapItem &_parent, StockRequest &&_request,
		LeasePtr &_lease_ref, StockGetHandler &_handler,
		CancellablePointer &_cancel_ptr) noexcept
		:parent(_parent), request(std::move(_request)),
		 lease_ref(_lease_ref), handler(_handler),
		 caller_cancel_ptr(_cancel_ptr)
	{
		caller_cancel_ptr = *this;
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		parent.RemoveWaiting(*this);
	}
};


MultiStock::MapItem::MapItem(StockMap &_map_stock, Stock &_stock) noexcept
	:map_stock(_map_stock), stock(_stock),
	retry_event(stock.GetEventLoop(), BIND_THIS_METHOD(RetryWaiting))
{
	map_stock.SetSticky(stock, true);
}

MultiStock::MapItem::~MapItem() noexcept
{
	assert(items.empty());
	assert(waiting.empty());

	map_stock.SetSticky(stock, false);

	if (get_cancel_ptr)
		get_cancel_ptr.Cancel();
}

MultiStock::SharedItem *
MultiStock::MapItem::FindUsable() noexcept
{	for (auto &i : items)
		if (i.CanUse())
			return &i;

	return nullptr;
}

MultiStock::SharedItem &
MultiStock::MapItem::GetNow(StockRequest request, std::size_t concurrency)
{
	if (auto *i = FindUsable())
		return *i;

	auto *stock_item = stock.GetNow(std::move(request));
	assert(stock_item != nullptr);

	auto *item = new SharedItem(*this, *stock_item, concurrency);
	items.push_back(*item);
	return *item;
}

inline void
MultiStock::MapItem::Get(StockRequest request, std::size_t concurrency,
			 LeasePtr &lease_ref,
			 StockGetHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept
{
	if (auto *i = FindUsable()) {
		i->AddLease(handler, lease_ref);
		return;
	}

	if (auto *stock_item = stock.GetIdle()) {
		auto *i = new SharedItem(*this, *stock_item, concurrency);
		items.push_back(*i);
		i->AddLease(handler, lease_ref);
		return;
	}

	get_concurrency = concurrency;

	auto *w = new Waiting(*this, std::move(request),
			      lease_ref, handler, cancel_ptr);
	waiting.push_back(*w);

	if (!stock.IsFull() && !get_cancel_ptr)
		stock.GetCreate(std::move(w->request), *this, get_cancel_ptr);
}

inline void
MultiStock::MapItem::RemoveItem(SharedItem &item) noexcept
{
	items.erase_and_dispose(items.iterator_to(item), DeleteDisposer{});

	if (items.empty() && !ScheduleRetryWaiting())
		delete this;
}

inline void
MultiStock::MapItem::RemoveWaiting(Waiting &w) noexcept
{
	waiting.erase_and_dispose(waiting.iterator_to(w), DeleteDisposer{});

	if (!waiting.empty())
		return;

	if (retry_event.IsPending()) {
		/* an item is ready, but was not yet delivered to the
		   Waiting; delete all empty items */
		retry_event.Cancel();

		DeleteEmptyItems();
	}

	if (items.empty())
		delete this;
	else if (get_cancel_ptr)
		get_cancel_ptr.CancelAndClear();
}

inline void
MultiStock::MapItem::DeleteEmptyItems(const SharedItem *except) noexcept
{
	items.remove_and_dispose_if([except](const auto &item){
		return &item != except && item.IsEmpty();
	}, DeleteDisposer{});
}

inline void
MultiStock::MapItem::FinishWaiting(SharedItem &item) noexcept
{
	assert(item.CanUse());
	assert(!waiting.empty());
	assert(!retry_event.IsPending());

	auto &w = waiting.front();
	/* if there is still a request object, move it to the
	   next item in the waiting list */
	if (w.request)
		if (auto n = std::next(waiting.begin());
		    n != waiting.end())
			n->request = std::move(w.request);

	auto &handler = w.handler;
	auto &lease_ref = w.lease_ref;
	waiting.pop_front_and_dispose(DeleteDisposer{});

	/* do it again until no more usable items are
	   found */
	if (!ScheduleRetryWaiting())
		/* no more waiting: we can now remove all
		   remaining idle items which havn't been
		   removed while there were still waiting
		   items, but we had more empty items than we
		   really needed */
		DeleteEmptyItems(&item);

	item.AddLease(handler, lease_ref);
}

inline void
MultiStock::MapItem::RetryWaiting() noexcept
{
	assert(!waiting.empty());

	if (auto *i = FindUsable()) {
		FinishWaiting(*i);
		return;
	}

	auto &w = waiting.front();
	assert(w.request);

	if (!stock.IsFull() && !get_cancel_ptr)
		stock.GetCreate(std::move(w.request), *this, get_cancel_ptr);
}

bool
MultiStock::MapItem::ScheduleRetryWaiting() noexcept
{
	if (waiting.empty())
		return false;

	retry_event.Schedule();
	return true;
}

void
MultiStock::MapItem::OnStockItemReady(StockItem &stock_item) noexcept
{
	assert(!waiting.empty());
	get_cancel_ptr = nullptr;

	retry_event.Cancel();

	auto *item = new SharedItem(*this, stock_item, get_concurrency);
	items.push_back(*item);

	FinishWaiting(*item);
}

void
MultiStock::MapItem::OnStockItemError(std::exception_ptr error) noexcept
{
	assert(!waiting.empty());
	get_cancel_ptr = nullptr;

	retry_event.Cancel();

	waiting.clear_and_dispose([&error](auto *w){
		w->handler.OnStockItemError(error);
		delete w;
	});

	if (items.empty())
		delete this;
}

inline void
MultiStock::MapItem::OnLeaseReleased(SharedItem &item) noexcept
{
	/* now that a lease was released, schedule the "waiting" list
	   again */
	if (ScheduleRetryWaiting() && item.CanUse())
		/* somebody's waiting and the iten can be reused for
		   them - don't try to delete the item, even if it's
		   empty */
		return;

	if (item.IsEmpty())
		RemoveItem(item);
}

inline std::size_t
MultiStock::MapItem::Hash::operator()(const char *key) const noexcept
{
	assert(key != nullptr);

	return djb_hash_string(key);
}

inline std::size_t
MultiStock::MapItem::Hash::operator()(const MapItem &value) const noexcept
{
	return (*this)(value.stock.GetName());
}

inline bool
MultiStock::MapItem::Equal::operator()(const char *a, const MapItem &b) const noexcept
{
	return StringIsEqual(a, b.stock.GetName());
}

inline bool
MultiStock::MapItem::Equal::operator()(const MapItem &a, const MapItem &b) const noexcept
{
	return (*this)(a.stock.GetName(), b);
}

MultiStock::MultiStock(StockMap &_hstock) noexcept
	:hstock(_hstock),
	 map(Map::bucket_traits(buckets, N_BUCKETS))
{
}

MultiStock::~MultiStock() noexcept
{
	/* by now, all leases must be freed */
	assert(map.empty());
}

MultiStock::MapItem &
MultiStock::MakeMapItem(const char *uri, void *request) noexcept
{
	Map::insert_commit_data hint;
	auto [i, inserted] =
		map.insert_check(uri, map.hash_function(), map.key_eq(), hint);
	if (inserted) {
		auto *item = new MapItem(hstock,
					 hstock.GetStock(uri, request));
		map.insert_commit(*item, hint);
		return *item;
	} else
		return *i;

}

StockItem *
MultiStock::GetNow(const char *uri, StockRequest request,
		   std::size_t concurrency,
		   LeasePtr &lease_ref)
{
	return MakeMapItem(uri, request.get())
		.GetNow(std::move(request), concurrency)
		.AddLease(lease_ref);
}

void
MultiStock::Get(const char *uri, StockRequest request,
		std::size_t concurrency,
		LeasePtr &lease_ref,
		StockGetHandler &handler,
		CancellablePointer &cancel_ptr) noexcept
{
	MakeMapItem(uri, request.get())
		.Get(std::move(request), concurrency,
		     lease_ref, handler, cancel_ptr);
}
