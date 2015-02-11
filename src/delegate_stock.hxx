/*
 * Delegate helper pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_STOCK_HXX
#define BENG_DELEGATE_STOCK_HXX

struct pool;
struct ChildOptions;
struct StockMap;
struct StockGetHandler;
struct StockItem;

StockMap *
delegate_stock_new(struct pool *pool);

void
delegate_stock_get(StockMap *delegate_stock, struct pool *pool,
                   const char *path,
                   const ChildOptions &options,
                   const StockGetHandler &handler, void *handler_ctx,
                   struct async_operation_ref &async_ref);

void
delegate_stock_put(StockMap *delegate_stock,
                   StockItem &item, bool destroy);

int
delegate_stock_item_get(StockItem &item);

#endif
