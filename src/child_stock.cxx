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

#include "child_stock.hxx"
#include "child_socket.hxx"
#include "spawn/ExitListener.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "pool.hxx"

#include <string>

#include <assert.h>
#include <unistd.h>

struct ChildStockItem final : HeapStockItem, ExitListener {
    SpawnService &spawn_service;

    ChildSocket socket;
    int pid = -1;

    bool busy = true;

    ChildStockItem(CreateStockItem c,
                   SpawnService &_spawn_service)
        :HeapStockItem(c),
         spawn_service(_spawn_service) {}

    ~ChildStockItem() override;

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        assert(!busy);
        busy = true;

        return true;
    }

    bool Release(gcc_unused void *ctx) override {
        assert(busy);
        busy = false;

        /* reuse this item only if the child process hasn't exited */
        return pid > 0;
    }

    /* virtual methods from class ExitListener */
    void OnChildProcessExit(int status) override;
};

class ChildStock {
    SpawnService &spawn_service;
    const ChildStockClass &cls;

public:
    explicit ChildStock(SpawnService &_spawn_service,
                        const ChildStockClass &_cls)
        :spawn_service(_spawn_service), cls(_cls) {}

    void Create(CreateStockItem c, void *info);

};

void
ChildStockItem::OnChildProcessExit(gcc_unused int status)
{
    pid = -1;

    if (!busy)
        InvokeIdleDisconnect();
}

/*
 * stock class
 *
 */

inline void
ChildStock::Create(CreateStockItem c, void *info)
{
    auto *item = new ChildStockItem(c, spawn_service);

    try {
        int socket_type = cls.socket_type != nullptr
            ? cls.socket_type(info)
            : SOCK_STREAM;

        auto fd = item->socket.Create(socket_type);

        PreparedChildProcess p;
        cls.prepare(info, std::move(fd), p);

        item->pid = spawn_service.SpawnChildProcess(item->GetStockName(),
                                                    std::move(p),
                                                    item);
    } catch (...) {
        delete item;
        throw;
    }

    item->InvokeCreateSuccess();
}

static void
child_stock_create(void *stock_ctx,
                   CreateStockItem c,
                   void *info,
                   gcc_unused struct pool &caller_pool,
                   gcc_unused CancellablePointer &cancel_ptr)
{
    auto &stock = *(ChildStock *)stock_ctx;

    stock.Create(c, info);
}

ChildStockItem::~ChildStockItem()
{
    if (pid >= 0)
        spawn_service.KillChildProcess(pid);

    if (socket.IsDefined())
        socket.Unlink();
}

static constexpr StockClass child_stock_class = {
    .create = child_stock_create,
};


/*
 * interface
 *
 */

StockMap *
child_stock_new(unsigned limit, unsigned max_idle,
                EventLoop &event_loop, SpawnService &spawn_service,
                const ChildStockClass *cls)
{
    assert(cls != nullptr);
    assert(cls->prepare != nullptr);

    auto *s = new ChildStock(spawn_service, *cls);
    return new StockMap(event_loop, child_stock_class, s, limit, max_idle);
}

void
child_stock_free(StockMap *stock)
{
    auto *s = (ChildStock *)stock->GetClassContext();
    delete stock;
    delete s;
}

UniqueSocketDescriptor
child_stock_item_connect(StockItem *_item)
{
    auto *item = (ChildStockItem *)_item;

    try {
        return item->socket.Connect();
    } catch (...) {
        /* if the connection fails, abandon the child process, don't
           try again - it will never work! */
        item->fade = true;
        throw;
    }
}
