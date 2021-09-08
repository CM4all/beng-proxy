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
MultiStock::Item::Lease::ReleaseLease(bool _reuse) noexcept
{
	item.DeleteLease(this, _reuse);
}

MultiStock::Item::~Item() noexcept
{
	assert(leases.empty());

	item.Put(!reuse);
}

void
MultiStock::Item::AddLease(StockGetHandler &handler,
			   LeasePtr &lease_ref) noexcept
{
	lease_ref.Set(AddLease());

	handler.OnStockItemReady(item);
}

inline void
MultiStock::Item::DeleteLease(Lease *lease, bool _reuse) noexcept
{
	reuse &= _reuse;

	assert(!leases.empty());
	leases.erase_and_dispose(leases.iterator_to(*lease),
				 DeleteDisposer());
	++remaining_leases;

	parent.OnLeaseReleased(*this);
}

MultiStock::Item *
MultiStock::MapItem::FindUsable() noexcept
{	for (auto &i : items)
		if (i.CanUse())
			return &i;

	return nullptr;
}

MultiStock::Item &
MultiStock::MapItem::GetNow(StockRequest request, unsigned max_leases)
{
	if (auto *i = FindUsable())
		return *i;

	auto *stock_item = stock.GetNow(std::move(request));
	assert(stock_item != nullptr);

	auto *item = new Item(*this, *stock_item, max_leases);
	items.push_back(*item);
	return *item;
}

inline void
MultiStock::MapItem::RemoveItem(Item &item) noexcept
{
	items.erase_and_dispose(items.iterator_to(item), DeleteDisposer{});

	if (items.empty())
		delete this;
}

inline void
MultiStock::MapItem::OnLeaseReleased(Item &item) noexcept
{
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
		auto *item = new MapItem(hstock.GetStock(uri, request));
		map.insert_commit(*item, hint);
		return *item;
	} else
		return *i;

}

StockItem *
MultiStock::GetNow(const char *uri, StockRequest request,
		   unsigned max_leases,
		   LeasePtr &lease_ref)
{
	return MakeMapItem(uri, request.get())
		.GetNow(std::move(request), max_leases)
		.AddLease(lease_ref);
}
