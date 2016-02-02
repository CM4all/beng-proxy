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
#include "lease.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <errno.h>

struct async_operation_ref;
struct StockMap;

struct DelegateGlue final : Lease {
    StockMap *const stock;
    StockItem *item;

    DelegateGlue(StockMap &_stock, StockItem &_item)
        :stock(&_stock), item(&_item) {}

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        delegate_stock_put(stock, *item, !reuse);
    }
};

void
delegate_stock_open(StockMap *stock, struct pool *pool,
                    const char *helper,
                    const ChildOptions &options,
                    const char *path,
                    DelegateHandler &handler,
                    struct async_operation_ref &async_ref)
{
    GError *error = nullptr;
    auto *item = delegate_stock_get(stock, pool, helper, options, &error);
    if (item == nullptr) {
        handler.OnDelegateError(error);
        return;
    }

    auto glue = NewFromPool<DelegateGlue>(*pool, *stock, *item);
    delegate_open(delegate_stock_item_get(*item), *glue,
                  pool, path,
                  handler, &async_ref);
}
