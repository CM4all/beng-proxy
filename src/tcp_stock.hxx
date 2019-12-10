/*
 * Copyright 2007-2019 CM4all GmbH
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

/*
 * TCP client connection pooling.
 */

#pragma once

#include "stock/Class.hxx"
#include "stock/MapStock.hxx"
#include "event/Chrono.hxx"
#include "util/Compiler.h"

class AllocatorPtr;
class SocketDescriptor;
class CancellablePointer;
class SocketAddress;
class EventLoop;
class StopwatchPtr;

/**
 * A TCP connection stock.
 *
 * @return the new TCP connections stock (this function cannot fail)
 */
class TcpStock final : StockClass {
	StockMap stock;

public:
	/**
	 * @param limit the maximum number of connections per host
	 */
	TcpStock(EventLoop &event_loop, unsigned limit)
		:stock(event_loop, *this, limit, 16) {}

	EventLoop &GetEventLoop() const noexcept {
		return stock.GetEventLoop();
	}

	void AddStats(StockStats &data) const {
		stock.AddStats(data);
	}

	/**
	 * @param name the MapStock name; it is auto-generated from the
	 * #address if nullptr is passed here
	 * @param timeout the connect timeout in seconds
	 */
	void Get(AllocatorPtr alloc, const StopwatchPtr &parent_stopwatch,
		 const char *name,
		 bool ip_transparent,
		 SocketAddress bind_address,
		 SocketAddress address,
		 Event::Duration timeout,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr);

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    CancellablePointer &cancel_ptr) override;
};

gcc_pure
SocketDescriptor
tcp_stock_item_get(const StockItem &item);

/**
 * Returns the (peer) address this object is connected to.
 */
gcc_pure
SocketAddress
tcp_stock_item_get_address(const StockItem &item);

gcc_pure
int
tcp_stock_item_get_domain(const StockItem &item);
