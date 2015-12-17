/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_CLASS_HXX
#define BENG_PROXY_STOCK_CLASS_HXX

struct pool;
struct async_operation_ref;
struct CreateStockItem;

struct StockClass {
    void (*create)(void *ctx, struct pool &parent_pool, CreateStockItem c,
                   const char *uri, void *info,
                   struct pool &caller_pool,
                   struct async_operation_ref &async_ref);
};

#endif
