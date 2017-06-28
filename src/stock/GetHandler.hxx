/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_GET_HANDLER_HXX
#define BENG_PROXY_STOCK_GET_HANDLER_HXX

#include <exception>

struct StockItem;

class StockGetHandler {
public:
    virtual void OnStockItemReady(StockItem &item) = 0;
    virtual void OnStockItemError(std::exception_ptr ep) = 0;
};

#endif
