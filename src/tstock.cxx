/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tstock.hxx"
#include "TranslateHandler.hxx"
#include "translate_client.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "stock/GetHandler.hxx"
#include "lease.hxx"
#include "pool.hxx"
#include "gerrno.h"
#include "net/AllocatedSocketAddress.hxx"
#include "net/SocketDescriptor.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"

#include <daemon/log.h>

#include <string.h>
#include <errno.h>

class TranslateConnection final : public HeapStockItem {
    SocketDescriptor s;

    Event event;

public:
    explicit TranslateConnection(CreateStockItem c)
        :HeapStockItem(c) {}

    ~TranslateConnection() override {
        if (s.IsDefined())
            event.Delete();
    }

private:
    bool CreateAndConnect(SocketAddress address) {
        assert(!s.IsDefined());

        return s.Create(AF_LOCAL, SOCK_STREAM, 0) &&
            s.Connect(address);
    }

public:
    void CreateAndConnectAndFinish(SocketAddress address) {
        if (CreateAndConnect(address)) {
            event.Set(s.Get(), EV_READ,
                      MakeSimpleEventCallback(TranslateConnection,
                                              EventCallback), this);
            InvokeCreateSuccess();
        } else {
            auto error = new_error_errno();

            if (s.IsDefined())
                s.Close();

            InvokeCreateError(error);
        }
    }

    int GetSocket() {
        return s.Get();
    }

private:
    void EventCallback() {
        char buffer;
        ssize_t nbytes = recv(s.Get(), &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle translation server connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data in idle translation server connection\n");

        InvokeIdleDisconnect();
        pool_commit();
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
              gcc_unused struct async_operation_ref &async_ref)
{
    const auto &address = *(const AllocatedSocketAddress *)info;

    auto *connection = new TranslateConnection(c);
    connection->CreateAndConnectAndFinish(address);
}

static constexpr StockClass tstock_class = {
    .create = tstock_create,
};

class TranslateStock {
    EventLoop &event_loop;

    Stock stock;

    AllocatedSocketAddress address;

public:
    TranslateStock(EventLoop &_event_loop, const char *path, unsigned limit)
        :event_loop(_event_loop),
         stock(event_loop, tstock_class, nullptr, "translation", limit, 8) {
        address.SetLocal(path);
    }

    EventLoop &GetEventLoop() {
        return event_loop;
    }

    void Get(struct pool &pool, StockGetHandler &handler,
             struct async_operation_ref &async_ref) {
        stock.Get(pool, &address, handler, async_ref);
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

    struct async_operation_ref &async_ref;

public:
    TranslateStockRequest(TranslateStock &_stock, struct pool &_pool,
                          const TranslateRequest &_request,
                          const TranslateHandler &_handler, void *_ctx,
                          struct async_operation_ref &_async_ref)
        :pool(_pool), stock(_stock),
         request(_request),
         handler(_handler), handler_ctx(_ctx),
         async_ref(_async_ref) {}

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
              async_ref);
}

void
TranslateStockRequest::OnStockItemError(GError *error)
{
    handler.error(error, handler_ctx);
}

/*
 * constructor
 *
 */

TranslateStock *
tstock_new(EventLoop &event_loop, const char *socket_path, unsigned limit)
{
    return new TranslateStock(event_loop, socket_path, limit);
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
                 struct async_operation_ref &async_ref)
{
    auto r = NewFromPool<TranslateStockRequest>(pool, stock, pool, request,
                                                handler, ctx, async_ref);
    stock.Get(pool, *r, async_ref);
}
