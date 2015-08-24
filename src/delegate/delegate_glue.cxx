/*
 * This helper library glues delegate_stock and delegate_client
 * together.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "delegate_glue.hxx"
#include "delegate_client.hxx"
#include "delegate_stock.hxx"
#include "stock.hxx"
#include "lease.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <errno.h>

struct async_operation_ref;
struct StockMap;

struct DelegateGlue {
    struct pool *pool;

    const char *path;

    StockMap *stock;
    StockItem *item;

    const struct delegate_handler *handler;
    void *handler_ctx;
    struct async_operation_ref *async_ref;
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

static void
delegate_stock_ready(StockItem &item, void *_ctx)
{
    DelegateGlue *glue = (DelegateGlue *)_ctx;

    glue->item = &item;

    delegate_open(delegate_stock_item_get(item),
                  &delegate_socket_lease, glue,
                  glue->pool, glue->path,
                  glue->handler, glue->handler_ctx, glue->async_ref);
}

static void
delegate_stock_error(GError *error, void *ctx)
{
    DelegateGlue *glue = (DelegateGlue *)ctx;

    glue->handler->error(error, glue->handler_ctx);
}

static constexpr StockGetHandler delegate_stock_handler = {
    .ready = delegate_stock_ready,
    .error = delegate_stock_error,
};

void
delegate_stock_open(StockMap *stock, struct pool *pool,
                    const char *helper,
                    const ChildOptions &options,
                    const char *path,
                    const struct delegate_handler *handler, void *ctx,
                    struct async_operation_ref &async_ref)
{
    auto glue = NewFromPool<DelegateGlue>(*pool);

    glue->pool = pool;
    glue->path = path;
    glue->stock = stock;
    glue->handler = handler;
    glue->handler_ctx = ctx;
    glue->async_ref = &async_ref;

    delegate_stock_get(stock, pool, helper, options,
                       delegate_stock_handler, glue, async_ref);
}
