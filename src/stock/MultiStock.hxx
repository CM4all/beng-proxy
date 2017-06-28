/*
 * A wrapper for #stock that allows multiple users of one stock_item.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_MULTI_STOCK_HXX
#define BENG_PROXY_MULTI_STOCK_HXX

#include <inline/compiler.h>

struct pool;
struct lease_ref;
class StockMap;
struct StockItem;
struct StockStats;
class MultiStock;

gcc_malloc
MultiStock *
mstock_new(StockMap &hstock);

void
mstock_free(MultiStock *mstock);

/**
 * Obtain statistics.
 */
gcc_pure
void
mstock_add_stats(const MultiStock &stock, StockStats &data);

/**
 * Obtains an item from the mstock without going through the callback.
 * This requires a stock class which finishes the create() method
 * immediately.
 *
 * Throws exception on error.
 *
 * @param max_leases the maximum number of leases per stock_item
 */
gcc_pure
StockItem *
mstock_get_now(MultiStock &mstock, struct pool &caller_pool,
               const char *uri, void *info, unsigned max_leases,
               struct lease_ref &lease_ref);

#endif
