/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
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
#include "gerrno.h"
#include "net/AllocatedSocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"

#include <daemon/log.h>

#include <stdexcept>

#include <string.h>
#include <errno.h>

class TranslateConnection final : public HeapStockItem {
    UniqueSocketDescriptor s;

    SocketEvent event;

public:
    explicit TranslateConnection(CreateStockItem c)
        :HeapStockItem(c),
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
            event.Set(s.Get(), EV_READ);
            InvokeCreateSuccess();
        } else {
            auto error = new_error_errno();

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
    bool Borrow(gcc_unused void *ctx) override {
        event.Delete();
        return true;
    }

    bool Release(gcc_unused void *ctx) override {
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
    void OnStockItemError(GError *error) override;

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
TranslateStockRequest::OnStockItemError(GError *error)
{
    handler.error(std::make_exception_ptr(std::runtime_error(error->message)),
                  handler_ctx);
    g_error_free(error);
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
