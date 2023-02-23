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

StockMap *
delegate_stock_new(EventLoop &event_loop, SpawnService &spawn_service);

void
delegate_stock_free(StockMap *stock);

/**
 * Throws exception on error.
 */
StockItem *
delegate_stock_get(StockMap *delegate_stock,
		   const char *path,
		   const ChildOptions &options);

SocketDescriptor
delegate_stock_item_get(StockItem &item) noexcept;
