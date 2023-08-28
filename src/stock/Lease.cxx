// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Lease.hxx"
#include "stock/Item.hxx"

void
StockItemLease::ReleaseLease(PutAction action) noexcept
{
	item.Put(action);
	Destroy();
}
