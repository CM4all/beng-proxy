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

#include "ChildStock.hxx"
#include "ChildStockItem.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/StringView.hxx"

#include <cassert>

int
ChildStockClass::GetChildSocketType(void *) const noexcept
{
	return SOCK_STREAM;
}

unsigned
ChildStockClass::GetChildBacklog(void *) const noexcept
{
	return 0;
}

StringView
ChildStockClass::GetChildTag(void *) const noexcept
{
	return nullptr;
}

/*
 * stock class
 *
 */

void
ChildStock::Create(CreateStockItem c, StockRequest request,
		   CancellablePointer &)
{
	auto *item = new ChildStockItem(c, *this, spawn_service,
					cls.GetChildTag(request.get()));

	try {
		item->Spawn(cls, request.get(), backlog,
			    log_socket, log_options);
	} catch (...) {
		delete item;
		throw;
	}

	item->InvokeCreateSuccess();
}

/*
 * interface
 *
 */

ChildStock::ChildStock(EventLoop &event_loop, SpawnService &_spawn_service,
		       ChildStockClass &_cls,
		       unsigned _backlog,
		       SocketDescriptor _log_socket,
		       const ChildErrorLogOptions &_log_options,
		       unsigned _limit, unsigned _max_idle) noexcept
	:map(event_loop, *this, _cls, _limit, _max_idle),
	 spawn_service(_spawn_service), cls(_cls),
	 backlog(_backlog),
	 log_socket(_log_socket),
	 log_options(_log_options)
{
}

ChildStock::~ChildStock() noexcept = default;

void
ChildStock::FadeTag(StringView tag) noexcept
{
	map.FadeIf([tag](const StockItem &_item) {
		const auto &item = (const ChildStockItem &)_item;
		return item.IsTag(tag);
	});
}

void
ChildStock::AddIdle(ChildStockItem &item) noexcept
{
	idle.push_back(item);
}

void
ChildStock::DiscardOldestIdle() noexcept
{
	if (idle.empty())
		return;

	/* the list front is the oldest item (the one that hasn't been
	   used for the longest time) */
	auto &item = idle.front();
	assert(item.is_idle);
	item.InvokeIdleDisconnect();
}

UniqueSocketDescriptor
child_stock_item_connect(StockItem &_item)
{
	auto &item = (ChildStockItem &)_item;

	return item.Connect();
}

StringView
child_stock_item_get_tag(const StockItem &_item)
{
	const auto &item = (const ChildStockItem &)_item;

	return item.GetTag();
}

UniqueFileDescriptor
child_stock_item_get_stderr(const StockItem &_item) noexcept
{
	const auto &item = (const ChildStockItem &)_item;

	return item.GetStderr();
}

void
child_stock_item_set_site(StockItem &_item, const char *site) noexcept
{
	auto &item = (ChildStockItem &)_item;
	item.SetSite(site);
}

void
child_stock_item_set_uri(StockItem &_item, const char *uri) noexcept
{
	auto &item = (ChildStockItem &)_item;
	item.SetUri(uri);
}
