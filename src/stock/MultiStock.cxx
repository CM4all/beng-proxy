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

#include "MultiStock.hxx"
#include "MapStock.hxx"
#include "GetHandler.hxx"
#include "Item.hxx"

MultiStock::Domain::Item::~Item()
{
    assert(leases.empty());

    item.Put(!reuse);
}

void
MultiStock::Domain::Item::AddLease(StockGetHandler &handler,
                                   struct lease_ref &lease_ref)
{
    lease_ref.Set(AddLease());

    handler.OnStockItemReady(item);
}

void
MultiStock::Domain::Item::DeleteLease(Lease *lease, bool _reuse)
{
    reuse &= _reuse;

    assert(!leases.empty());
    leases.erase_and_dispose(leases.iterator_to(*lease),
                             DeleteDisposer());

    if (leases.empty())
        domain->second.DeleteItem(*this);
}

StockItem *
MultiStock::Domain::GetNow(DomainMap::iterator di,
                           struct pool &caller_pool,
                           const char *uri, void *info,
                           unsigned max_leases,
                           struct lease_ref &lease_ref)
{
    auto i = FindUsableItem();
    if (i == nullptr) {
        auto *item = stock.hstock.GetNow(caller_pool, uri, info);
        i = new Item(di, max_leases, *item);
        items.push_front(*i);
    }

    return i->AddLease(lease_ref);
}

StockItem *
MultiStock::GetNow(struct pool &caller_pool, const char *uri, void *info,
                   unsigned max_leases,
                   struct lease_ref &lease_ref)
{
    auto di = domains.emplace(uri, *this).first;
    return di->second.GetNow(di, caller_pool, uri, info, max_leases,
                             lease_ref);
}
