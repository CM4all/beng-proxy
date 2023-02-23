// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Lease.hxx"
#include "stock/Item.hxx"

void
StockItemLease::ReleaseLease(bool reuse) noexcept
{
	item.Put(!reuse);
	Destroy();
}
