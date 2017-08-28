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

#include "lhttp_stock.hxx"
#include "lhttp_address.hxx"
#include "stock/Stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/MultiStock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "lease.hxx"
#include "child_stock.hxx"
#include "spawn/JailParams.hxx"
#include "spawn/Prepared.hxx"
#include "event/SocketEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/Logger.hxx"
#include "util/RuntimeError.hxx"
#include "util/Exception.hxx"

#include <assert.h>
#include <unistd.h>
#include <string.h>

class LhttpStock final : StockClass, ChildStockClass {
    ChildStock child_stock;
    MultiStock mchild_stock;
    StockMap hstock;

public:
    LhttpStock(unsigned limit, unsigned max_idle,
               EventLoop &event_loop, SpawnService &spawn_service);

    void FadeAll() {
        hstock.FadeAll();
        child_stock.GetStockMap().FadeAll();
        mchild_stock.FadeAll();
    }

    StockMap &GetConnectionStock() {
        return hstock;
    }

private:
    /* virtual methods from class StockClass */
    void Create(CreateStockItem c, void *info, struct pool &caller_pool,
                CancellablePointer &cancel_ptr) override;

    /* virtual methods from class ChildStockClass */
    int GetChildSocketType(void *info) const noexcept override;
    void PrepareChild(void *info, UniqueSocketDescriptor &&fd,
                      PreparedChildProcess &p) override;
};

class LhttpConnection final : LoggerDomainFactory, StockItem {
    LazyDomainLogger logger;

    StockItem *child = nullptr;

    struct lease_ref lease_ref;

    UniqueSocketDescriptor fd;
    SocketEvent event;

public:
    explicit LhttpConnection(CreateStockItem c)
        :StockItem(c),
         logger(*this),
         event(c.stock.GetEventLoop(), BIND_THIS_METHOD(EventCallback)) {}

    ~LhttpConnection() override;

    void Connect(MultiStock &child_stock, struct pool &caller_pool,
                 const char *key, void *info,
                 unsigned concurrency);

    SocketDescriptor GetSocket() const {
        assert(fd.IsDefined());
        return fd;
    }

private:
    void EventCallback(unsigned events);

    /* virtual methods from LoggerDomainFactory */
    std::string MakeLoggerDomain() const noexcept override {
        return GetStockName();
    }

    /* virtual methods from class StockItem */
    bool Borrow() override {
        event.Delete();
        return true;
    }

    bool Release() override {
        event.Add(EventDuration<300>::value);
        return true;
    }
};

void
LhttpConnection::Connect(MultiStock &child_stock, struct pool &caller_pool,
                         const char *key, void *info,
                         unsigned concurrency)
{
    try {
        child = child_stock.GetNow(caller_pool,
                                   key, info, concurrency,
                                   lease_ref);
    } catch (...) {
        delete this;
        std::throw_with_nested(FormatRuntimeError("Failed to launch LHTTP server '%s'",
                                                  key));
    }

    try {
        fd = child_stock_item_connect(child);
    } catch (...) {
        delete this;
        std::throw_with_nested(FormatRuntimeError("Failed to connect to LHTTP server '%s'",
                                                  key));
    }

    event.Set(fd.Get(), SocketEvent::READ);
    InvokeCreateSuccess();
}

static const char *
lhttp_stock_key(struct pool *pool, const LhttpAddress *address)
{
    return address->GetServerId(pool);
}

/*
 * libevent callback
 *
 */

inline void
LhttpConnection::EventCallback(unsigned events)
{
    if ((events & SocketEvent::TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes = fd.Read(&buffer, sizeof(buffer));
        if (nbytes < 0)
            logger(2, "error on idle LHTTP connection: ", strerror(errno));
        else if (nbytes > 0)
            logger(2, "unexpected data from idle LHTTP connection");
    }

    InvokeIdleDisconnect();
}

/*
 * child_stock class
 *
 */

int
LhttpStock::GetChildSocketType(void *info) const noexcept
{
    const auto &address = *(const LhttpAddress *)info;

    int type = SOCK_STREAM;
    if (!address.blocking)
        type |= SOCK_NONBLOCK;

    return type;
}

void
LhttpStock::PrepareChild(void *info, UniqueSocketDescriptor &&fd,
                         PreparedChildProcess &p)
{
    const auto &address = *(const LhttpAddress *)info;

    p.SetStdin(std::move(fd));
    address.CopyTo(p);
}

/*
 * stock class
 *
 */

void
LhttpStock::Create(CreateStockItem c, void *info,
                   struct pool &caller_pool,
                   gcc_unused CancellablePointer &cancel_ptr)
{
    const auto *address = (const LhttpAddress *)info;

    assert(address != nullptr);
    assert(address->path != nullptr);

    auto *connection = new LhttpConnection(c);

    connection->Connect(mchild_stock, caller_pool,
                        c.GetStockName(), info, address->concurrency);
}

LhttpConnection::~LhttpConnection()
{
    if (fd.IsDefined()) {
        event.Delete();
        fd.Close();
    }

    if (child != nullptr)
        lease_ref.Release(true);
}


/*
 * interface
 *
 */

inline
LhttpStock::LhttpStock(unsigned limit, unsigned max_idle,
                       EventLoop &event_loop, SpawnService &spawn_service)
    :child_stock(event_loop, spawn_service,
                 *this,
                 limit, max_idle),
     mchild_stock(child_stock.GetStockMap()),
     hstock(event_loop, *this, limit, max_idle) {}

LhttpStock *
lhttp_stock_new(unsigned limit, unsigned max_idle,
                EventLoop &event_loop, SpawnService &spawn_service)
{
    return new LhttpStock(limit, max_idle, event_loop, spawn_service);
}

void
lhttp_stock_free(LhttpStock *ls)
{
    delete ls;
}

void
lhttp_stock_fade_all(LhttpStock &ls)
{
    ls.FadeAll();
}

StockItem *
lhttp_stock_get(LhttpStock *lhttp_stock, struct pool *pool,
                const LhttpAddress *address)
{
    const auto *const jail = address->options.jail;
    if (jail != nullptr && jail->enabled && jail->home_directory == nullptr)
        throw std::runtime_error("No home directory for jailed LHTTP");

    union {
        const LhttpAddress *in;
        void *out;
    } deconst = { .in = address };

    return lhttp_stock->GetConnectionStock().GetNow(*pool,
                                                    lhttp_stock_key(pool, address),
                                                    deconst.out);
}

SocketDescriptor
lhttp_stock_item_get_socket(const StockItem &item)
{
    const auto *connection = (const LhttpConnection *)&item;

    return connection->GetSocket();
}

FdType
lhttp_stock_item_get_type(gcc_unused const StockItem &item)
{
    return FdType::FD_SOCKET;
}
