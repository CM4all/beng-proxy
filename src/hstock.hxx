/*
 * The StockMap class is a hash table of any number of Stock objects,
 * each with a different URI.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HSTOCK_HXX
#define BENG_PROXY_HSTOCK_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

struct pool;
struct async_operation_ref;
struct StockClass;
struct StockItem;
struct StockStats;
struct StockGetHandler;
struct StockMap;

gcc_malloc
StockMap *
hstock_new(struct pool &pool,
           const StockClass &_class, void *class_ctx,
           unsigned limit, unsigned max_idle);

void
hstock_free(StockMap *hstock);

/**
 * @see stock_fade_all()
 */
void
hstock_fade_all(StockMap &hstock);

/**
 * Obtain statistics.
 */
gcc_pure
void
hstock_add_stats(const StockMap &stock, StockStats &data);

void
hstock_get(StockMap &hstock, struct pool &pool,
           const char *uri, void *info,
           const StockGetHandler &handler, void *handler_ctx,
           struct async_operation_ref &async_ref);

/**
 * Obtains an item from the hstock without going through the callback.
 * This requires a stock class which finishes the create() method
 * immediately.
 */
gcc_pure
StockItem *
hstock_get_now(StockMap &hstock, struct pool &pool,
               const char *uri, void *info,
               GError **error_r);

void
hstock_put(StockMap &hstock, const char *uri, StockItem &item,
           bool destroy);


#endif
