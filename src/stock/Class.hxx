/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_CLASS_HXX
#define BENG_PROXY_STOCK_CLASS_HXX

class CancellablePointer;
struct CreateStockItem;

struct StockClass {
    void (*create)(void *ctx, CreateStockItem c,
                   void *info,
                   struct pool &caller_pool,
                   CancellablePointer &cancel_ptr);
};

#endif
