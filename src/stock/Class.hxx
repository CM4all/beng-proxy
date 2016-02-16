/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_CLASS_HXX
#define BENG_PROXY_STOCK_CLASS_HXX

struct async_operation_ref;
struct CreateStockItem;

struct StockClass {
    void (*create)(void *ctx, CreateStockItem c,
                   void *info,
                   struct pool &caller_pool,
                   struct async_operation_ref &async_ref);
};

#endif
