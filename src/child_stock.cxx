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
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "pool.hxx"

#include <string>

#include <assert.h>
#include <unistd.h>

int
ChildStockClass::GetChildSocketType(void *) const noexcept
{
    return SOCK_STREAM;
}

const char *
ChildStockClass::GetChildTag(void *) const noexcept
{
    return nullptr;
}

struct ChildStockItem final : StockItem, ExitListener {
    SpawnService &spawn_service;

    const std::string tag;

    ChildSocket socket;
    int pid = -1;

    bool busy = true;

    ChildStockItem(CreateStockItem c,
                   SpawnService &_spawn_service,
                   const char *_tag) noexcept
        :StockItem(c),
         spawn_service(_spawn_service),
         tag(_tag != nullptr ? _tag : "") {}

    ~ChildStockItem() override;

    /* virtual methods from class StockItem */
    bool Borrow() override {
        assert(!busy);
        busy = true;

        return true;
    }

    bool Release() override {
        assert(busy);
        busy = false;

        /* reuse this item only if the child process hasn't exited */
        return pid > 0;
    }

    /* virtual methods from class ExitListener */
    void OnChildProcessExit(int status) override;
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

void
ChildStock::Create(CreateStockItem c, void *info,
                   struct pool &, CancellablePointer &)
{
    auto *item = new ChildStockItem(c, spawn_service,
                                    cls.GetChildTag(info));

    try {
        int socket_type = cls.GetChildSocketType(info);

        auto fd = item->socket.Create(socket_type);

        PreparedChildProcess p;
        cls.PrepareChild(info, std::move(fd), p);

        item->pid = spawn_service.SpawnChildProcess(item->GetStockName(),
                                                    std::move(p),
                                                    item);
    } catch (...) {
        delete item;
        throw;
    }

    item->InvokeCreateSuccess();
}

ChildStockItem::~ChildStockItem()
{
    if (pid >= 0)
        spawn_service.KillChildProcess(pid);

    if (socket.IsDefined())
        socket.Unlink();
}

/*
 * interface
 *
 */

ChildStock::ChildStock(EventLoop &event_loop, SpawnService &_spawn_service,
                       ChildStockClass &_cls,
                       unsigned _limit, unsigned _max_idle) noexcept
    :map(event_loop, *this, _limit, _max_idle),
     spawn_service(_spawn_service), cls(_cls)
{
}

void
ChildStock::FadeTag(const char *tag)
{
    map.FadeIf([tag](const StockItem &_item) {
            const auto &item = (const ChildStockItem &)_item;
            return item.tag == tag;
        });
}

UniqueSocketDescriptor
child_stock_item_connect(StockItem &_item)
{
    auto &item = (ChildStockItem &)_item;

    try {
        return item.socket.Connect();
    } catch (...) {
        /* if the connection fails, abandon the child process, don't
           try again - it will never work! */
        item.fade = true;
        throw;
    }
}

const char *
child_stock_item_get_tag(const StockItem &_item)
{
    const auto &item = (const ChildStockItem &)_item;

    return item.tag.empty() ? nullptr : item.tag.c_str();
}
