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
#include "access_log/ChildErrorLog.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "pool/pool.hxx"

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

class ChildStockItem final : public StockItem, ExitListener {
    SpawnService &spawn_service;

    const std::string tag;

    ChildErrorLog log;

    ChildSocket socket;
    int pid = -1;

    bool busy = true;

public:
    ChildStockItem(CreateStockItem c,
                   SpawnService &_spawn_service,
                   const char *_tag) noexcept
        :StockItem(c),
         spawn_service(_spawn_service),
         tag(_tag != nullptr ? _tag : "") {}

    ~ChildStockItem() override;

    EventLoop &GetEventLoop() {
        return stock.GetEventLoop();
    }

    void Spawn(ChildStockClass &cls, void *info,
               SocketDescriptor log_socket);

    gcc_pure
    const char *GetTag() const {
        return tag.empty() ? nullptr : tag.c_str();
    }

    gcc_pure
    bool IsTag(const char *_tag) const {
        return tag == _tag;
    }

    void SetSite(const char *site) noexcept {
        log.SetSite(site);
    }

    void SetUri(const char *uri) noexcept {
        log.SetUri(uri);
    }

    UniqueSocketDescriptor Connect() {
        try {
            return socket.Connect();
        } catch (...) {
            /* if the connection fails, abandon the child process, don't
               try again - it will never work! */
            fade = true;
            throw;
        }
    }

    /* virtual methods from class StockItem */
    bool Borrow() noexcept override {
        assert(!busy);
        busy = true;

        return true;
    }

    bool Release() noexcept override {
        assert(busy);
        busy = false;

        /* reuse this item only if the child process hasn't exited */
        return pid > 0;
    }

private:
    /* virtual methods from class ExitListener */
    void OnChildProcessExit(int status) override;
};

void
ChildStockItem::Spawn(ChildStockClass &cls, void *info,
                      SocketDescriptor log_socket)
{
    int socket_type = cls.GetChildSocketType(info);

    PreparedChildProcess p;
    cls.PrepareChild(info, socket.Create(socket_type), p);

    if (log_socket.IsDefined() && p.stderr_fd < 0)
        log.EnableClient(p, GetEventLoop(), log_socket);

    pid = spawn_service.SpawnChildProcess(GetStockName(), std::move(p), this);
}

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
        item->Spawn(cls, info, log_socket);
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
                       SocketDescriptor _log_socket,
                       unsigned _limit, unsigned _max_idle) noexcept
    :map(event_loop, *this, _limit, _max_idle),
     spawn_service(_spawn_service), cls(_cls),
     log_socket(_log_socket)
{
}

void
ChildStock::FadeTag(const char *tag)
{
    map.FadeIf([tag](const StockItem &_item) {
            const auto &item = (const ChildStockItem &)_item;
            return item.IsTag(tag);
        });
}

UniqueSocketDescriptor
child_stock_item_connect(StockItem &_item)
{
    auto &item = (ChildStockItem &)_item;

    return item.Connect();
}

const char *
child_stock_item_get_tag(const StockItem &_item)
{
    const auto &item = (const ChildStockItem &)_item;

    return item.GetTag();
}

void
child_stock_item_set_site(StockItem &_item, const char *site) noexcept
{
    auto &item = (ChildStockItem &)_item;
    item.SetSite(site);
}

void
child_stock_item_set_uri(StockItem &_item, const char *uri) noexcept
{
    auto &item = (ChildStockItem &)_item;
    item.SetUri(uri);
}
