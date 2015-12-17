/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_CLASS_HXX
#define BENG_PROXY_STOCK_CLASS_HXX

struct pool;
struct async_operation_ref;
struct CreateStockItem;

struct StockClass {
    struct pool *(*pool)(void *ctx, struct pool &parent, const char *uri);
    void (*create)(void *ctx, struct pool &pool, CreateStockItem c,
                   const char *uri, void *info,
                   struct pool &caller_pool,
                   struct async_operation_ref &async_ref);
};

#endif
