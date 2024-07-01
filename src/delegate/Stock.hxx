// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct ChildOptions;
class StockMap;
class EventLoop;
class SpawnService;
class SocketDescriptor;
class StockItem;
class StockGetHandler;
class AllocatorPtr;
class CancellablePointer;

StockMap *
delegate_stock_new(EventLoop &event_loop, SpawnService &spawn_service) noexcept;

void
delegate_stock_free(StockMap *stock) noexcept;

void
delegate_stock_get(StockMap &delegate_stock, AllocatorPtr alloc,
		   const char *path,
		   const ChildOptions &options,
		   StockGetHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept;

[[gnu::pure]]
SocketDescriptor
delegate_stock_item_get(StockItem &item) noexcept;
