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

#include "MapStock.hxx"
#include "util/djbhash.h"
#include "util/DeleteDisposer.hxx"

#include <assert.h>

inline size_t
StockMap::Item::KeyHasher(const char *key) noexcept
{
	assert(key != nullptr);

	return djb_hash_string(key);
}

StockMap::~StockMap() noexcept
{
	map.clear_and_dispose(DeleteDisposer());
}

void
StockMap::Erase(Item &item) noexcept
{
	auto i = map.iterator_to(item);
	map.erase_and_dispose(i, DeleteDisposer());
}

void
StockMap::OnStockEmpty(Stock &stock) noexcept
{
	auto &item = Item::Cast(stock);

	logger.Format(5, "hstock(%p) remove empty stock(%p, '%s')",
		      (const void *)this, (const void *)&stock, stock.GetName());

	Erase(item);
}

Stock &
StockMap::GetStock(const char *uri, void *request) noexcept
{
	Map::insert_commit_data hint;
	auto i = map.insert_check(uri, Item::KeyHasher, Item::KeyValueEqual, hint);
	if (i.second) {
		auto *item = new Item(event_loop, cls,
				      uri, limit, max_idle,
				      GetClearInterval(request),
				      this);
		map.insert_commit(*item, hint);
		return item->stock;
	} else
		return i.first->stock;

}
