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

#include "Stock.hxx"
#include "translation/Handler.hxx"
#include "Client.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "stock/GetHandler.hxx"
#include "lease.hxx"
#include "pool.hxx"
#include "system/Error.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"

#include <daemon/log.h>

#include <stdexcept>

#include <string.h>
#include <errno.h>

class TranslateConnection final : public StockItem {
    UniqueSocketDescriptor s;

    SocketEvent event;

public:
    explicit TranslateConnection(CreateStockItem c)
        :StockItem(c),
         event(c.stock.GetEventLoop(), BIND_THIS_METHOD(EventCallback)) {}

    ~TranslateConnection() override {
        if (s.IsDefined())
            event.Delete();
    }

private:
    bool CreateAndConnect(SocketAddress address) {
        assert(!s.IsDefined());

        return s.CreateNonBlock(AF_LOCAL, SOCK_STREAM, 0) &&
            s.Connect(address);
    }

public:
    void CreateAndConnectAndFinish(SocketAddress address) {
        if (CreateAndConnect(address)) {
            event.Set(s.Get(), SocketEvent::READ);
            InvokeCreateSuccess();
        } else {
            auto error = std::make_exception_ptr(MakeErrno());

            if (s.IsDefined())
                s.Close();

            InvokeCreateError(error);
        }
    }

    SocketDescriptor GetSocket() {
        return s;
    }

private:
    void EventCallback(unsigned) {
        char buffer;
        ssize_t nbytes = recv(s.Get(), &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle translation server connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data in idle translation server connection\n");

        InvokeIdleDisconnect();
    }

public:
    /* virtual methods from class StockItem */
    bool Borrow() override {
        event.Delete();
        return true;
    }

    bool Release() override {
        event.Add();
        return true;
    }
};

static void
tstock_create(gcc_unused void *ctx,
              CreateStockItem c,
              void *info,
              gcc_unused struct pool &caller_pool,
              gcc_unused CancellablePointer &cancel_ptr)
{
    const auto &address = *(const AllocatedSocketAddress *)info;

    auto *connection = new TranslateConnection(c);
    connection->CreateAndConnectAndFinish(address);
}

static constexpr StockClass tstock_class = {
    .create = tstock_create,
};

class TranslateStock {
    Stock stock;

    AllocatedSocketAddress address;

public:
    TranslateStock(EventLoop &event_loop, SocketAddress _address,
                   unsigned limit)
        :stock(event_loop, tstock_class, nullptr, "translation", limit, 8),
         address(_address) {
    }

    EventLoop &GetEventLoop() {
        return stock.GetEventLoop();
    }

    void Get(struct pool &pool, StockGetHandler &handler,
             CancellablePointer &cancel_ptr) {
        stock.Get(pool, &address, handler, cancel_ptr);
    }

    void Put(StockItem &item, bool destroy) {
        stock.Put(item, destroy);
    }
};

class TranslateStockRequest final : public StockGetHandler, Lease {
    struct pool &pool;

    TranslateStock &stock;
    TranslateConnection *item;

    const TranslateRequest &request;

    const TranslateHandler &handler;
    void *handler_ctx;

    CancellablePointer &cancel_ptr;

public:
    TranslateStockRequest(TranslateStock &_stock, struct pool &_pool,
                          const TranslateRequest &_request,
                          const TranslateHandler &_handler, void *_ctx,
                          CancellablePointer &_cancel_ptr)
        :pool(_pool), stock(_stock),
         request(_request),
         handler(_handler), handler_ctx(_ctx),
         cancel_ptr(_cancel_ptr) {}

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(std::exception_ptr ep) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        stock.Put(*item, !reuse);
    }
};


/*
 * stock callback
 *
 */

void
TranslateStockRequest::OnStockItemReady(StockItem &_item)
{
    item = &(TranslateConnection &)_item;
    translate(pool, stock.GetEventLoop(), item->GetSocket(),
              *this,
              request, handler, handler_ctx,
              cancel_ptr);
}

void
TranslateStockRequest::OnStockItemError(std::exception_ptr ep)
{
    handler.error(ep, handler_ctx);
}

/*
 * constructor
 *
 */

TranslateStock *
tstock_new(EventLoop &event_loop, SocketAddress address, unsigned limit)
{
    return new TranslateStock(event_loop, address, limit);
}

void
tstock_free(TranslateStock *stock)
{
    delete stock;
}

void
tstock_translate(TranslateStock &stock, struct pool &pool,
                 const TranslateRequest &request,
                 const TranslateHandler &handler, void *ctx,
                 CancellablePointer &cancel_ptr)
{
    auto r = NewFromPool<TranslateStockRequest>(pool, stock, pool, request,
                                                handler, ctx, cancel_ptr);
    stock.Get(pool, *r, cancel_ptr);
}
