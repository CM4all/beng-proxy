// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "io/FdType.hxx"

#include <string_view>

struct pool;
struct ChildErrorLogOptions;
class LhttpStock;
class StockGetHandler;
class CancellablePointer;
class StockItem;
struct LhttpAddress;
class SocketDescriptor;
class EventLoop;
class SpawnService;
class ListenStreamSpawnStock;

/**
 * Launch and manage "Local HTTP" child processes.
 */
LhttpStock *
lhttp_stock_new(unsigned limit, unsigned max_idle,
		EventLoop &event_loop, SpawnService &spawn_service,
		ListenStreamSpawnStock *listen_stream_spawn_stock,
		SocketDescriptor log_socket,
		const ChildErrorLogOptions &log_options) noexcept;

void
lhttp_stock_free(LhttpStock *lhttp_stock) noexcept;

/**
 * Discard one or more processes to free some memory.
 */
void
lhttp_stock_discard_some(LhttpStock &ls) noexcept;

void
lhttp_stock_fade_all(LhttpStock &ls) noexcept;

void
lhttp_stock_fade_tag(LhttpStock &ls, std::string_view tag) noexcept;

void
lhttp_stock_get(LhttpStock *lhttp_stock,
		const LhttpAddress *address,
		StockGetHandler &handler,
		CancellablePointer &cancel_ptr) noexcept;

/**
 * Returns the socket descriptor of the specified stock item.
 */
[[gnu::pure]]
SocketDescriptor
lhttp_stock_item_get_socket(const StockItem &item) noexcept;

/**
 * Abandon the socket.  Invoke this if the socket returned by
 * lhttp_stock_item_get_socket() has been closed by its caller.
 */
void
lhttp_stock_item_abandon_socket(StockItem &item) noexcept;

[[gnu::pure]]
FdType
lhttp_stock_item_get_type(const StockItem &item) noexcept;

void
lhttp_stock_item_set_site(StockItem &item, const char *site) noexcept;

void
lhttp_stock_item_set_uri(StockItem &item, const char *uri) noexcept;
