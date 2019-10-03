/*
 * Copyright 2007-2019 Content Management AG
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
#include "translation/Request.hxx"
#include "Client.hxx"
#include "stock/Item.hxx"
#include "stock/GetHandler.hxx"
#include "lease.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "system/Error.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"
#include "io/Logger.hxx"
#include "stopwatch.hxx"

#include <stdexcept>

#include <string.h>
#include <errno.h>

class TranslationStock::Connection final : public StockItem {
    UniqueSocketDescriptor s;

    SocketEvent event;

public:
    explicit Connection(CreateStockItem c) noexcept
        :StockItem(c),
         event(c.stock.GetEventLoop(), BIND_THIS_METHOD(EventCallback)) {}

private:
    bool CreateAndConnect(SocketAddress a) noexcept {
        assert(!s.IsDefined());

        return s.CreateNonBlock(AF_LOCAL, SOCK_STREAM, 0) &&
            s.Connect(a);
    }

public:
    void CreateAndConnectAndFinish(SocketAddress a) noexcept {
        if (CreateAndConnect(a)) {
            event.Open(s);
            InvokeCreateSuccess();
        } else {
            auto error = std::make_exception_ptr(MakeErrno("Failed to connect to translation server"));

            if (s.IsDefined())
                s.Close();

            InvokeCreateError(error);
        }
    }

    SocketDescriptor GetSocket() noexcept {
        return s;
    }

private:
    void EventCallback(unsigned) noexcept {
        char buffer;
        ssize_t nbytes = recv(s.Get(), &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            LogConcat(2, "translation",
                      "error on idle translation server connection: ",
                      strerror(errno));
        else if (nbytes > 0)
            LogConcat(2, "translation",
                      "unexpected data in idle translation server connection");

        InvokeIdleDisconnect();
    }

public:
    /* virtual methods from class StockItem */
    bool Borrow() noexcept override {
        event.Cancel();
        return true;
    }

    bool Release() noexcept override {
        event.ScheduleRead();
        return true;
    }
};

class TranslationStock::Request final
    : Cancellable, StockGetHandler, Lease, PoolLeakDetector
{
    struct pool &pool;

    StopwatchPtr stopwatch;

    TranslationStock &stock;
    Connection *item;

    const TranslateRequest &request;

    const TranslateHandler &handler;
    void *handler_ctx;

    CancellablePointer &caller_cancel_ptr;
    CancellablePointer cancel_ptr;

public:
    Request(TranslationStock &_stock, struct pool &_pool,
            const TranslateRequest &_request,
            const StopwatchPtr &parent_stopwatch,
            const TranslateHandler &_handler, void *_ctx,
            CancellablePointer &_cancel_ptr) noexcept
        :PoolLeakDetector(_pool),
         pool(_pool),
         stopwatch(parent_stopwatch, "translate",
                   _request.GetDiagnosticName()),
         stock(_stock),
         request(_request),
         handler(_handler), handler_ctx(_ctx),
         caller_cancel_ptr(_cancel_ptr)
    {
        _cancel_ptr = *this;
    }

    void Start() noexcept {
        stock.Get(*this, cancel_ptr);
    }

private:
    void Destroy() noexcept {
        this->~Request();
    }

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        /* this cancels only the TranslationStock::Get() call initiated
           from Start() */

        cancel_ptr.Cancel();
        Destroy();
    }

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) noexcept override;
    void OnStockItemError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) noexcept override {
        stock.Put(*item, !reuse);
        Destroy();
    }
};

/*
 * stock callback
 *
 */

void
TranslationStock::Request::OnStockItemReady(StockItem &_item) noexcept
{
    item = &(Connection &)_item;

    /* cancellation will not be handled by this class from here on;
       instead, we pass the caller's CancellablePointer to
       translate() */
    translate(pool, stock.GetEventLoop(), std::move(stopwatch),
              item->GetSocket(),
              *this,
              request, handler, handler_ctx,
              caller_cancel_ptr);

    /* ReleaseLease() will invoke Destroy() */
}

void
TranslationStock::Request::OnStockItemError(std::exception_ptr ep) noexcept
{
    auto &_handler = handler;
    auto *_handler_ctx = handler_ctx;
    Destroy();
    _handler.error(ep, _handler_ctx);
}

void
TranslationStock::Create(CreateStockItem c, StockRequest,
                         CancellablePointer &)
{
    auto *connection = new Connection(c);
    connection->CreateAndConnectAndFinish(address);
}

void
TranslationStock::SendRequest(struct pool &pool,
                              const TranslateRequest &request,
                              const StopwatchPtr &parent_stopwatch,
                              const TranslateHandler &handler, void *ctx,
                              CancellablePointer &cancel_ptr) noexcept
{
    auto r = NewFromPool<Request>(pool, *this, pool, request,
                                  parent_stopwatch,
                                  handler, ctx, cancel_ptr);
    r->Start();
}
