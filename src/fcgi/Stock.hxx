/*
 * Copyright 2007-2018 Content Management AG
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

struct ChildErrorLogOptions;
struct StockItem;
class FcgiStock;
struct ChildOptions;
class SocketDescriptor;
template<typename T> struct ConstBuffer;
class AllocatorPtr;
class EventLoop;
class SpawnService;
class SocketDescriptor;

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

SocketDescriptor
fcgi_stock_get_log_socket(const FcgiStock &fs) noexcept;

const ChildErrorLogOptions &
fcgi_stock_get_log_options(const FcgiStock &fs) noexcept;

void
fcgi_stock_fade_all(FcgiStock &fs) noexcept;

void
fcgi_stock_fade_tag(FcgiStock &fs, const char *tag) noexcept;

/**
 * Throws exception on error.
 *
 * @param args command-line arguments
 */
StockItem *
fcgi_stock_get(FcgiStock *fcgi_stock,
	       const ChildOptions &options,
	       const char *executable_path,
	       ConstBuffer<const char *> args);

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

/**
 * Translates a path into the application's namespace.
 */
const char *
fcgi_stock_translate_path(const StockItem &item,
			  const char *path, AllocatorPtr alloc) noexcept;

/**
 * Let the fcgi_stock know that the client is being aborted.  The
 * fcgi_stock may then figure out that the client process is faulty
 * and kill it at the next chance.  Note that this function will not
 * release the process - fcgi_stock_put() stil needs to be called
 * after this function.
 */
void
fcgi_stock_aborted(StockItem &item) noexcept;
