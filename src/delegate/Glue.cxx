/*
 * This helper library glues delegate_stock and delegate_client
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Glue.hxx"
#include "Client.hxx"
#include "Handler.hxx"
#include "Stock.hxx"
#include "stock.hxx"
#include "lease.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <errno.h>

struct async_operation_ref;
struct StockMap;

struct DelegateGlue final : StockGetHandler, Lease {
    struct pool *const pool;

    const char *const path;

    StockMap *const stock;
    StockItem *item;

    DelegateHandler &handler;
    struct async_operation_ref *const async_ref;

    DelegateGlue(StockMap &_stock, struct pool &_pool,
                 const char *_path,
                 DelegateHandler &_handler,
                 struct async_operation_ref &_async_ref)
        :pool(&_pool), path(_path), stock(&_stock),
         handler(_handler), async_ref(&_async_ref) {}

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        delegate_stock_put(stock, *item, !reuse);
    }
};

void
DelegateGlue::OnStockItemReady(StockItem &_item)
{
    item = &_item;

    delegate_open(delegate_stock_item_get(_item), *this,
                  pool, path,
                  handler, async_ref);
}

void
DelegateGlue::OnStockItemError(GError *error)
{
    handler.OnDelegateError(error);
}

void
delegate_stock_open(StockMap *stock, struct pool *pool,
                    const char *helper,
                    const ChildOptions &options,
                    const char *path,
                    DelegateHandler &handler,
                    struct async_operation_ref &async_ref)
{
    auto glue = NewFromPool<DelegateGlue>(*pool, *stock, *pool, path,
                                          handler, async_ref);

    delegate_stock_get(stock, pool, helper, options, *glue, async_ref);
}
