/*
 * Copyright 2007-2017 Content Management AG
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
 * Launch processes and connect a stream socket to them.
 */

#ifndef BENG_PROXY_CHILD_STOCK_HXX
#define BENG_PROXY_CHILD_STOCK_HXX

#include "stock/Class.hxx"
#include "stock/MapStock.hxx"
#include "io/FdType.hxx"

struct PreparedChildProcess;
class UniqueSocketDescriptor;
class EventLoop;
class SpawnService;

class ChildStockClass {
public:
    virtual int GetChildSocketType(void *info) const noexcept;

    /**
     * Throws std::runtime_error on error.
     */
    virtual void PrepareChild(void *info, UniqueSocketDescriptor &&fd,
                              PreparedChildProcess &p) = 0;
};

class ChildStock final : StockClass {
    StockMap map;

    SpawnService &spawn_service;
    ChildStockClass &cls;

public:
    ChildStock(EventLoop &event_loop, SpawnService &_spawn_service,
               ChildStockClass &_cls,
               unsigned _limit, unsigned _max_idle) noexcept;

    StockMap &GetStockMap() noexcept {
        return map;
    }

private:
    /* virtual methods from class StockClass */
    void Create(CreateStockItem c, void *info, struct pool &caller_pool,
                CancellablePointer &cancel_ptr) override;
};

/**
 * Connect a socket to the given child process.  The socket must be
 * closed before the #stock_item is returned.
 *
 * Throws std::runtime_error on error.
 *
 * @return a socket descriptor
 */
UniqueSocketDescriptor
child_stock_item_connect(StockItem &item);

static constexpr inline FdType
child_stock_item_get_type(const StockItem &)
{
    return FdType::FD_SOCKET;
}

#endif
