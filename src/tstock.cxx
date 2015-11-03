/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tstock.hxx"
#include "TranslateHandler.hxx"
#include "translate_client.hxx"
#include "stock/GetHandler.hxx"
#include "tcp_stock.hxx"
#include "lease.hxx"
#include "pool.hxx"
#include "net/AllocatedSocketAddress.hxx"

#include <daemon/log.h>

class TranslateStock {
    StockMap &tcp_stock;

    AllocatedSocketAddress address;

    const char *const address_string;

public:
    TranslateStock(StockMap &_tcp_stock, const char *path)
        :tcp_stock(_tcp_stock), address_string(path) {
        address.SetLocal(path);
    }

    void Get(struct pool &pool, StockGetHandler &handler,
             struct async_operation_ref &async_ref) {
        tcp_stock_get(&tcp_stock, &pool, address_string,
                      false, SocketAddress::Null(),
                      address,
                      10,
                      handler, async_ref);
    }

    void Put(StockItem &item, bool destroy) {
        tcp_stock_put(&tcp_stock, item, destroy);
    }
};

class TranslateStockRequest final : public StockGetHandler, Lease {
    struct pool &pool;

    TranslateStock &stock;
    StockItem *item;

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
    item = &_item;
    translate(pool, tcp_stock_item_get(_item),
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
tstock_new(struct pool &pool, StockMap &tcp_stock, const char *socket_path)
{
    return NewFromPool<TranslateStock>(pool, tcp_stock, socket_path);
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
