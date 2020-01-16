/*
 * Copyright 2007-2020 CM4all GmbH
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
#include "MapStock.hxx"
#include "GetHandler.hxx"
#include "Item.hxx"

bool
MultiStock::Item::Compare::Less(const char *a, const char *b) const
{
	return strcmp(a, b) < 0;
}

MultiStock::Item::~Item()
{
	assert(leases.empty());

	item.Put(!reuse);
}

const char *
MultiStock::Item::GetKey() const
{
	return item.GetStockName();
}

void
MultiStock::Item::AddLease(StockGetHandler &handler,
			   LeasePtr &lease_ref)
{
	lease_ref.Set(AddLease());

	handler.OnStockItemReady(item);
}

void
MultiStock::Item::DeleteLease(Lease *lease, bool _reuse)
{
	reuse &= _reuse;

	assert(!leases.empty());
	leases.erase_and_dispose(leases.iterator_to(*lease),
				 DeleteDisposer());
	++remaining_leases;

	if (leases.empty())
		delete this;
}

MultiStock::Item &
MultiStock::MakeItem(const char *uri, StockRequest request,
		     unsigned max_leases)
{
	auto i = items.lower_bound(uri, items.key_comp());
	for (; i != items.end() && !items.key_comp()(uri, *i); ++i)
		if (i->CanUse())
			return *i;

	auto *stock_item = hstock.GetNow(uri, std::move(request));
	assert(stock_item != nullptr);

	auto *item = new Item(max_leases, *stock_item);
	items.insert(i, *item);
	return *item;
}

StockItem *
MultiStock::GetNow(const char *uri, StockRequest request,
		   unsigned max_leases,
		   LeasePtr &lease_ref)
{
	return MakeItem(uri, std::move(request), max_leases).AddLease(lease_ref);
}
