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

StringView
ChildStockClass::GetChildTag(void *) const noexcept
{
	return nullptr;
}

std::unique_ptr<ChildStockItem>
ChildStockClass::CreateChild(CreateStockItem c, void *info,
			     ChildStock &child_stock)
{
	return std::make_unique<ChildStockItem>(c, child_stock,
						GetChildTag(info));
}

ChildStock::ChildStock(SpawnService &_spawn_service,
		       ChildStockClass &_cls,
		       SocketDescriptor _log_socket,
		       const ChildErrorLogOptions &_log_options) noexcept
	:spawn_service(_spawn_service), cls(_cls),
	 log_socket(_log_socket),
	 log_options(_log_options) {}

ChildStock::~ChildStock() noexcept = default;

/*
 * stock class
 *
 */

void
ChildStock::Create(CreateStockItem c, StockRequest request,
		   CancellablePointer &)
{
	auto item = cls.CreateChild(c, request.get(), *this);
	item->Spawn(cls, request.get(),
		    log_socket, log_options);

	item.release()->InvokeCreateSuccess();
}

/*
 * interface
 *
 */

ChildStockMap::ChildStockMap(EventLoop &event_loop, SpawnService &_spawn_service,
			     ChildStockClass &_cls,
			     SocketDescriptor _log_socket,
			     const ChildErrorLogOptions &_log_options,
			     unsigned _limit, unsigned _max_idle) noexcept
	:cls(_spawn_service, _cls, _log_socket, _log_options),
	 map(event_loop, cls, _cls, _limit, _max_idle)
{
}

void
ChildStockMap::FadeTag(StringView tag) noexcept
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
