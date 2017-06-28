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
#include "GException.hxx"
#include "stock/Item.hxx"
#include "stock/MapStock.hxx"
#include "lease.hxx"
#include "pool.hxx"

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
                    CancellablePointer &cancel_ptr)
{
    StockItem *item;

    try {
        item = delegate_stock_get(stock, pool, helper, options);
    } catch (...) {
        handler.OnDelegateError(std::current_exception());
        return;
    }

    auto glue = NewFromPool<DelegateGlue>(*pool, *item);
    delegate_open(stock->GetEventLoop(), delegate_stock_item_get(*item), *glue,
                  pool, path,
                  handler, cancel_ptr);
}
