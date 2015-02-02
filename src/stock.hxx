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

#include <boost/intrusive/list.hpp>

#include <stddef.h>

struct pool;
struct async_operation_ref;
struct StockItem;
struct Stock;

struct StockHandler {
    /**
     * The stock has become empty.  It is safe to delete it from
     * within this method.
     */
    void (*empty)(Stock &stock, const char *uri, void *ctx);
};

struct StockGetHandler {
    void (*ready)(StockItem &item, void *ctx);
    void (*error)(GError *error, void *ctx);
};

struct StockItem
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    Stock *stock;
    struct pool *pool;

    const StockGetHandler *handler;
    void *handler_ctx;

#ifndef NDEBUG
    bool is_idle;
#endif
};

struct StockClass {
    size_t item_size;

    struct pool *(*pool)(void *ctx, struct pool &parent, const char *uri);
    void (*create)(void *ctx, StockItem &item,
                   const char *uri, void *info,
                   struct pool &caller_pool,
                   struct async_operation_ref &async_ref);
    bool (*borrow)(void *ctx, StockItem &item);
    void (*release)(void *ctx, StockItem &item);
    void (*destroy)(void *ctx, StockItem &item);
};

struct StockStats {
    unsigned busy, idle;
};

/**
 * @param handler optional handler class
 */
Stock *
stock_new(struct pool &pool, const StockClass &_class,
          void *class_ctx, const char *uri, unsigned limit, unsigned max_idle,
          const StockHandler *handler, void *handler_ctx);

/**
 * @param handler optional handler class
 */
static inline Stock *
stock_new(struct pool &pool, const StockClass &_class,
          void *class_ctx, const char *uri, unsigned limit, unsigned max_idle,
          const StockHandler &handler, void *handler_ctx)
{
    return stock_new(pool, _class, class_ctx, uri, limit, max_idle,
                     &handler, handler_ctx);
}

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
gcc_pure
void
stock_add_stats(const Stock &stock, StockStats &data);

void
stock_get(Stock &stock, struct pool &pool, void *info,
          const StockGetHandler &handler, void *handler_ctx,
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
