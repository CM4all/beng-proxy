// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstddef>
#include <string_view>

namespace Net::Log { class Sink; }
struct ChildErrorLogOptions;
class LhttpStock;
class StockGetHandler;
class CancellablePointer;
struct LhttpAddress;
class EventLoop;
class SpawnService;
class ListenStreamStock;

/**
 * Launch and manage "Local HTTP" child processes.
 */
LhttpStock *
lhttp_stock_new(unsigned limit, unsigned max_idle,
		EventLoop &event_loop, SpawnService &spawn_service,
		ListenStreamStock *listen_stream_stock,
		Net::Log::Sink *log_sink,
		const ChildErrorLogOptions &log_options) noexcept;

void
lhttp_stock_free(LhttpStock *lhttp_stock) noexcept;

/**
 * Discard one or more processes to free some memory.
 */
std::size_t
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
