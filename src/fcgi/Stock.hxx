// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <span>
#include <string_view>

class CancellablePointer;
struct ChildErrorLogOptions;
class StockItem;
class StockGetHandler;
class FcgiStock;
struct ChildOptions;
class EventLoop;
class SpawnService;
class SocketDescriptor;
class UniqueFileDescriptor;

/**
 * Launch and manage FastCGI child processes.
 */
FcgiStock *
fcgi_stock_new(unsigned limit, unsigned max_idle,
	       EventLoop &event_loop, SpawnService &spawn_service,
	       SocketDescriptor log_socket,
	       const ChildErrorLogOptions &log_options) noexcept;

void
fcgi_stock_free(FcgiStock *fcgi_stock) noexcept;

[[gnu::const]]
EventLoop &
fcgi_stock_get_event_loop(const FcgiStock &fs) noexcept;

void
fcgi_stock_fade_all(FcgiStock &fs) noexcept;

void
fcgi_stock_fade_tag(FcgiStock &fs, std::string_view tag) noexcept;

/**
 * @param args command-line arguments
 */
void
fcgi_stock_get(FcgiStock *fcgi_stock,
	       const ChildOptions &options,
	       const char *executable_path,
	       std::span<const char *const> args,
	       unsigned parallelism,
	       StockGetHandler &handler,
	       CancellablePointer &cancel_ptr) noexcept;

void
fcgi_stock_item_set_site(StockItem &item, const char *site) noexcept;

void
fcgi_stock_item_set_uri(StockItem &item, const char *uri) noexcept;

/**
 * Returns the socket descriptor of the specified stock item.
 */
SocketDescriptor
fcgi_stock_item_get(const StockItem &item) noexcept;

int
fcgi_stock_item_get_domain(const StockItem &item) noexcept;

UniqueFileDescriptor
fcgi_stock_item_get_stderr(const StockItem &item) noexcept;

/**
 * Let the fcgi_stock know that the client is being aborted.  The
 * fcgi_stock may then figure out that the client process is faulty
 * and kill it at the next chance.  Note that this function will not
 * release the process - fcgi_stock_put() stil needs to be called
 * after this function.
 */
void
fcgi_stock_aborted(StockItem &item) noexcept;
