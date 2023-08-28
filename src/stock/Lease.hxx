// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "lease.hxx"

class StockItem;

/**
 * A #Lease implementation which calls StockItem::Put() and then
 * destructs itself.  It should be allocated from a pool, because it
 * does not free its own memory.
 */
class StockItemLease final : public Lease {
	StockItem &item;

public:
	explicit StockItemLease(StockItem &_item):item(_item) {}

	void Destroy() noexcept {
		this->~StockItemLease();
	}

	StockItem &GetItem() {
		return item;
	}

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override;
};
