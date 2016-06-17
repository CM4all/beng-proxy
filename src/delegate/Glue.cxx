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
#include "stock/Item.hxx"
#include "stock/MapStock.hxx"
#include "lease.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <errno.h>

struct async_operation_ref;

struct DelegateGlue final : Lease {
    StockItem &item;

    explicit DelegateGlue(StockItem &_item):item(_item) {}

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        item.Put(!reuse);
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

    auto glue = NewFromPool<DelegateGlue>(*pool, *item);
    delegate_open(stock->GetEventLoop(), delegate_stock_item_get(*item), *glue,
                  pool, path,
                  handler, &async_ref);
}
