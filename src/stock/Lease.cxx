// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Lease.hxx"
#include "stock/Item.hxx"

PutAction
StockItemLease::ReleaseLease(PutAction action) noexcept
{
	auto &_item = item;
	Destroy();
	return _item.Put(action);
}
