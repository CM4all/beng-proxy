// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ChildStock.hxx"
#include "ChildStockItem.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"

#include <cassert>

std::string_view
ChildStockClass::GetChildTag(void *) const noexcept
{
	return {};
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
		   StockGetHandler &handler,
		   CancellablePointer &)
{
	auto item = cls.CreateChild(c, request.get(), *this);
	item->Spawn(cls, request.get(),
		    log_socket, log_options);

	item.release()->InvokeCreateSuccess(handler);
}

/*
 * interface
 *
 */

ChildStockMap::ChildStockMap(EventLoop &event_loop, SpawnService &_spawn_service,
			     ChildStockMapClass &_cls,
			     SocketDescriptor _log_socket,
			     const ChildErrorLogOptions &_log_options,
			     unsigned _limit, unsigned _max_idle) noexcept
	:cls(_spawn_service, _cls, _log_socket, _log_options),
	 map(event_loop, cls, _cls, _limit, _max_idle)
{
}

void
ChildStockMap::FadeTag(std::string_view tag) noexcept
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
