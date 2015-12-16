/*
 * Objects in stock.  May be used for connection pooling.
 *
 * The 'stock' class holds a number of idle objects.  The URI may be
 * something like a hostname:port pair for HTTP client connections -
 * it is not used by this class, but passed to the stock_class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOCK_HXX
#define BENG_PROXY_STOCK_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct async_operation_ref;
struct StockItem;
struct Stock;
struct StockItem;
struct StockStats;
struct StockClass;
struct CreateStockItem;
class StockGetHandler;

class StockHandler {
public:
    /**
     * The stock has become empty.  It is safe to delete it from
     * within this method.
     */
    virtual void OnStockEmpty(Stock &stock, const char *uri) = 0;
};

/**
 * @param handler optional handler
 */
Stock *
stock_new(struct pool &pool, const StockClass &_class,
          void *class_ctx, const char *uri, unsigned limit, unsigned max_idle,
          StockHandler *handler=nullptr);

void
stock_free(Stock *stock);

gcc_pure
const char *
stock_get_uri(Stock &stock);

/**
 * Returns true if there are no items in the stock - neither idle nor
 * busy.
 */
gcc_pure
bool
stock_is_empty(const Stock &stock);

/**
 * Obtain statistics.
 */
void
stock_add_stats(const Stock &stock, StockStats &data);

/**
 * Destroy all idle items and don't reuse any of the current busy
 * items.
 */
void
stock_fade_all(Stock &stock);

void
stock_get(Stock &stock, struct pool &pool, void *info,
          StockGetHandler &handler,
          struct async_operation_ref &async_ref);

/**
 * Obtains an item from the stock without going through the callback.
 * This requires a stock class which finishes the create() method
 * immediately.
 */
gcc_pure
StockItem *
stock_get_now(Stock &stock, struct pool &pool, void *info, GError **error_r);

void
stock_item_available(StockItem &item);

void
stock_item_failed(StockItem &item, GError *error);

void
stock_item_aborted(StockItem &item);

void
stock_put(StockItem &item, bool destroy);

void
stock_del(StockItem &item);

#endif
