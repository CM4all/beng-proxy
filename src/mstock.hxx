/*
 * A wrapper for #stock that allows multiple users of one stock_item.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_MSTOCK_HXX
#define BENG_PROXY_MSTOCK_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

struct pool;
struct async_operation_ref;
struct hstock;
struct lease_ref;
struct StockItem;
struct StockStats;
struct StockGetHandler;

gcc_malloc
struct mstock *
mstock_new(struct hstock &hstock);

void
mstock_free(struct mstock *mstock);

/**
 * Obtain statistics.
 */
gcc_pure
void
mstock_add_stats(const struct mstock &stock, StockStats &data);

/**
 * Obtains an item from the mstock without going through the callback.
 * This requires a stock class which finishes the create() method
 * immediately.
 *
 * @param max_leases the maximum number of leases per stock_item
 */
gcc_pure
StockItem *
mstock_get_now(struct mstock &mstock, struct pool &caller_pool,
               const char *uri, void *info, unsigned max_leases,
               struct lease_ref &lease_ref,
               GError **error_r);


#endif
