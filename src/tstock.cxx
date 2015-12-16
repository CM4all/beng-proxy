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

class TranslateConnection final : public PoolStockItem {
    SocketDescriptor s;

    Event event;

public:
    explicit TranslateConnection(CreateStockItem c)
        :PoolStockItem(c) {
    }

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

static struct pool *
tstock_pool(gcc_unused void *ctx, struct pool &parent,
            gcc_unused const char *uri)
{
    return pool_new_linear(&parent, "tstock", 512);
}

static void
tstock_create(gcc_unused void *ctx, CreateStockItem c,
              gcc_unused const char *uri, void *info,
              gcc_unused struct pool &caller_pool,
              gcc_unused struct async_operation_ref &async_ref)
{
    const auto &address = *(const AllocatedSocketAddress *)info;

    auto *connection = NewFromPool<TranslateConnection>(c.pool, c);
    connection->CreateAndConnectAndFinish(address);
}

static constexpr StockClass tstock_class = {
    .pool = tstock_pool,
    .create = tstock_create,
};

class TranslateStock {
    Stock *const stock;

    AllocatedSocketAddress address;

public:
    TranslateStock(struct pool &p, const char *path, unsigned limit)
        :stock(stock_new(p, tstock_class, nullptr, nullptr, limit, 8)) {
        address.SetLocal(path);
    }

    ~TranslateStock() {
        stock_free(stock);
    }

    void Get(struct pool &pool, StockGetHandler &handler,
             struct async_operation_ref &async_ref) {
        stock_get(*stock, pool, &address, handler, async_ref);
    }

    void Put(StockItem &item, bool destroy) {
        stock_put(item, destroy);
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
    translate(pool, item->GetSocket(),
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
tstock_new(struct pool &pool, const char *socket_path, unsigned limit)
{
    return NewFromPool<TranslateStock>(pool, pool, socket_path, limit);
}

void
tstock_free(struct pool &pool, TranslateStock *stock)
{
    DeleteFromPool(pool, stock);
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
