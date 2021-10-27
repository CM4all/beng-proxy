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

MultiStock::SharedItem::SharedItem(MapItem &_parent, StockItem &_item,
				   std::size_t _limit) noexcept
	:parent(_parent), shared_item(_item),
	 limit(_limit),
	 cleanup_timer(shared_item.stock.GetEventLoop(),
		       BIND_THIS_METHOD(OnCleanupTimer))
{
}

MultiStock::SharedItem::~SharedItem() noexcept
{
	assert(busy.empty());

	DiscardUnused();

	shared_item.Put(true);
}

inline bool
MultiStock::SharedItem::CanUse() const noexcept
{
	return !shared_item.fade && !IsFull();
}

inline bool
MultiStock::SharedItem::ShouldDelete() const noexcept
{
	return shared_item.fade && IsEmpty();
}

void
MultiStock::SharedItem::OnCleanupTimer() noexcept
{
	if (IsEmpty()) {
		/* if this item was unused for one cleanup_timer
		   period, let parent.OnLeaseReleased() discard it */
		shared_item.fade = true;
		parent.OnLeaseReleased(*this);
		return;
	}

	/* destroy one third of the idle items */
	for (std::size_t i = (idle.size() + 2) / 3; i > 0; --i)
		idle.pop_front_and_dispose(DeleteDisposer{});

	/* repeat until we need this SharedItem again or until there
	   are no more idle items */
	ScheduleCleanupTimer();
}

void
MultiStock::SharedItem::DiscardUnused() noexcept
{
	idle.clear_and_dispose(DeleteDisposer{});
}

void
MultiStock::SharedItem::Fade() noexcept
{
	shared_item.fade = true;
	DiscardUnused();

	if (IsEmpty())
		/* let the parent destroy us */
		ScheduleCleanupNow();
}

inline void
MultiStock::SharedItem::CreateLease(MultiStockClass &_cls,
				    StockGetHandler &handler) noexcept
try {
	auto *lease = _cls.Create({*this, handler}, shared_item);
	lease->InvokeCreateSuccess();
} catch (...) {
	ItemCreateError(handler, std::current_exception());
}

inline StockItem *
MultiStock::SharedItem::GetIdle() noexcept
{
	assert(CanUse());

	// TODO code copied from Stock::GetIdle()

	auto i = idle.begin();
	const auto end = idle.end();
	while (i != end) {
		StockItem &item = *i;
		assert(item.is_idle);

		if (item.unclean) {
			/* postpone reusal of this item until it's
			   "clean" */
			// TODO: replace this kludge
			++i;
			continue;
		}

		i = idle.erase(i);

		if (item.Borrow()) {
#ifndef NDEBUG
			item.is_idle = false;
#endif

			CancelCleanupTimer();

			busy.push_front(item);
			return &item;
		}

		delete &item;
	}

	return nullptr;
}

inline bool
MultiStock::SharedItem::GetIdle(StockGetHandler &handler) noexcept
{
	assert(CanUse());

	auto *item = GetIdle();
	if (item == nullptr)
		return false;

	handler.OnStockItemReady(*item);
	return true;
}

void
MultiStock::SharedItem::GetLease(MultiStockClass &_cls,
				 StockGetHandler &handler) noexcept
{
	assert(CanUse());

	if (!GetIdle(handler))
		CreateLease(_cls, handler);
}

const char *
MultiStock::SharedItem::GetName() const noexcept
{
	return shared_item.stock.GetName();
}

EventLoop &
MultiStock::SharedItem::GetEventLoop() const noexcept
{
	return cleanup_timer.GetEventLoop();
}

void
MultiStock::SharedItem::Put(StockItem &item, bool destroy) noexcept
{
	assert(!item.is_idle);
	assert(&item.stock == this);

	assert(!busy.empty());

	busy.erase(busy.iterator_to(item));

	if (shared_item.fade || destroy || item.fade || !item.Release()) {
		delete &item;
	} else {
#ifndef NDEBUG
		item.is_idle = true;
#endif

		idle.push_front(item);

		ScheduleCleanupTimer();
	}

	parent.OnLeaseReleased(*this);
}

void
MultiStock::SharedItem::ItemIdleDisconnect(StockItem &item) noexcept
{
	assert(item.is_idle);
	assert(!idle.empty());

	idle.erase_and_dispose(idle.iterator_to(item), DeleteDisposer{});

	if (IsEmpty())
		parent.RemoveItem(*this);
}

void
MultiStock::SharedItem::ItemCreateSuccess(StockItem &item) noexcept
{
	busy.push_front(item);
	item.handler.OnStockItemReady(item);
}

void
MultiStock::SharedItem::ItemCreateError(StockGetHandler &get_handler,
					std::exception_ptr ep) noexcept
{
	Fade();

	if (IsEmpty())
		parent.OnLeaseReleased(*this);

	get_handler.OnStockItemError(std::move(ep));
}

void
MultiStock::SharedItem::ItemCreateAborted() noexcept
{
	// unreachable
}

void
MultiStock::SharedItem::ItemUncleanFlagCleared() noexcept
{
	parent.OnLeaseReleased(*this);
}

struct MultiStock::MapItem::Waiting final
	: boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
		  Cancellable
{
	MapItem &parent;
	StockRequest request;
	StockGetHandler &handler;
	CancellablePointer &caller_cancel_ptr;

	Waiting(MapItem &_parent, StockRequest &&_request,
		StockGetHandler &_handler,
		CancellablePointer &_cancel_ptr) noexcept
		:parent(_parent), request(std::move(_request)),
		 handler(_handler),
		 caller_cancel_ptr(_cancel_ptr)
	{
		caller_cancel_ptr = *this;
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		parent.RemoveWaiting(*this);
	}
};

MultiStock::MapItem::MapItem(StockMap &_map_stock, Stock &_stock,
			     MultiStockClass &_cls) noexcept
	:map_stock(_map_stock), stock(_stock), cls(_cls),
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
{
	for (auto &i : items)
		if (i.CanUse())
			return &i;

	return nullptr;
}

inline void
MultiStock::MapItem::Get(StockRequest request, std::size_t concurrency,
			 StockGetHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept
{
	if (auto *i = FindUsable()) {
		i->GetLease(cls, handler);
		return;
	}

	if (auto *stock_item = stock.GetIdle()) {
		auto *i = new SharedItem(*this, *stock_item, concurrency);
		items.push_back(*i);
		i->CreateLease(cls, handler);
		return;
	}

	get_concurrency = concurrency;

	auto *w = new Waiting(*this, std::move(request),
			      handler, cancel_ptr);
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


void
MultiStock::MapItem::DiscardUnused() noexcept
{
	items.remove_and_dispose_if([](const auto &item){
		return !item.IsBusy();
	}, DeleteDisposer{});
}

inline void
MultiStock::MapItem::FinishWaiting(SharedItem &item) noexcept
{
	assert(item.CanUse());
	assert(!waiting.empty());
	assert(!retry_event.IsPending());

	auto &w = waiting.front();

	auto &handler = w.handler;
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

	item.GetLease(cls, handler);
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

	if (item.ShouldDelete())
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

MultiStock::MultiStock(StockMap &_hstock, MultiStockClass &_cls) noexcept
	:hstock(_hstock), cls(_cls),
	 map(Map::bucket_traits(buckets, N_BUCKETS))
{
}

MultiStock::~MultiStock() noexcept
{
	DiscardUnused();

	/* by now, all leases must be freed */
	assert(map.empty());
}

void
MultiStock::DiscardUnused() noexcept
{
	for (auto i = map.begin(), end = map.end(); i != end;) {
		auto next = std::next(i);

		i->DiscardUnused();
		if (i->IsEmpty())
			map.erase_and_dispose(i, DeleteDisposer{});

		i = next;
	}
}

MultiStock::MapItem &
MultiStock::MakeMapItem(const char *uri, void *request) noexcept
{
	Map::insert_commit_data hint;
	auto [i, inserted] =
		map.insert_check(uri, map.hash_function(), map.key_eq(), hint);
	if (inserted) {
		auto *item = new MapItem(hstock,
					 hstock.GetStock(uri, request),
					 cls);
		map.insert_commit(*item, hint);
		return *item;
	} else
		return *i;

}

void
MultiStock::Get(const char *uri, StockRequest request,
		std::size_t concurrency,
		StockGetHandler &handler,
		CancellablePointer &cancel_ptr) noexcept
{
	MakeMapItem(uri, request.get())
		.Get(std::move(request), concurrency,
		     handler, cancel_ptr);
}
