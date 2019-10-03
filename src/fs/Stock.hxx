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

/*
 * Stock for FilteredSocket instances.
 */

#pragma once

#include "stock/Class.hxx"
#include "stock/MapStock.hxx"
#include "util/Compiler.h"

struct pool;
class StockMap;
struct StockItem;
class StockGetHandler;
class CancellablePointer;
class SocketFilterFactory;
struct FilteredSocket;
class EventLoop;
class SocketAddress;

/**
 * A stock for TCP connections wrapped with #FilteredSocket.
 */
class FilteredSocketStock final : StockClass {
    StockMap stock;

public:
    /**
     * @param limit the maximum number of connections per host
     */
    FilteredSocketStock(EventLoop &event_loop, unsigned limit) noexcept
        :stock(event_loop, *this, limit, 16) {}

    EventLoop &GetEventLoop() noexcept {
        return stock.GetEventLoop();
    }

    void AddStats(StockStats &data) const noexcept {
        stock.AddStats(data);
    }

    /**
     * @param name the MapStock name; it is auto-generated from the
     * #address if nullptr is passed here
     * @param timeout the connect timeout in seconds
     */
    void Get(struct pool &pool, const char *name,
             bool ip_transparent,
             SocketAddress bind_address,
             SocketAddress address,
             Event::Duration timeout,
             SocketFilterFactory *filter_factory,
             StockGetHandler &handler,
             CancellablePointer &cancel_ptr) noexcept;

private:
    /* virtual methods from class StockClass */
    void Create(CreateStockItem c, StockRequest request,
                CancellablePointer &cancel_ptr) override;
};

gcc_pure
FilteredSocket &
fs_stock_item_get(StockItem &item);

/**
 * Returns the (peer) address this object is connected to.
 */
gcc_pure
SocketAddress
fs_stock_item_get_address(const StockItem &item);
