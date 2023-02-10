/*
 * Copyright 2007-2022 CM4all GmbH
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

/**
 * Launch and manage "Local HTTP" child processes.
 */
LhttpStock *
lhttp_stock_new(unsigned limit, unsigned max_idle,
		EventLoop &event_loop, SpawnService &spawn_service,
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
