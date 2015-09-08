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

struct DelegateGlue final : StockGetHandler {
    struct pool *const pool;

    const char *const path;

    StockMap *const stock;
    StockItem *item;

    const struct delegate_handler *const handler;
    void *const handler_ctx;
    struct async_operation_ref *const async_ref;

    DelegateGlue(StockMap &_stock, struct pool &_pool,
                 const char *_path,
                 const struct delegate_handler &_handler, void *_ctx,
                 struct async_operation_ref &_async_ref)
        :pool(&_pool), path(_path), stock(&_stock),
         handler(&_handler), handler_ctx(_ctx),
         async_ref(&_async_ref) {}

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;
};

static void
delegate_socket_release(bool reuse, void *ctx)
{
    DelegateGlue *glue = (DelegateGlue *)ctx;

    delegate_stock_put(glue->stock, *glue->item, !reuse);
}

static const struct lease delegate_socket_lease = {
    .release = delegate_socket_release,
};

void
DelegateGlue::OnStockItemReady(StockItem &_item)
{
    item = &_item;

    delegate_open(delegate_stock_item_get(_item),
                  &delegate_socket_lease, this,
                  pool, path,
                  handler, handler_ctx, async_ref);
}

void
DelegateGlue::OnStockItemError(GError *error)
{
    handler->error(error, handler_ctx);
}

void
delegate_stock_open(StockMap *stock, struct pool *pool,
                    const char *helper,
                    const ChildOptions &options,
                    const char *path,
                    const struct delegate_handler *handler, void *ctx,
                    struct async_operation_ref &async_ref)
{
    auto glue = NewFromPool<DelegateGlue>(*pool, *stock, *pool, path,
                                          *handler, ctx, async_ref);

    delegate_stock_get(stock, pool, helper, options, *glue, async_ref);
}
