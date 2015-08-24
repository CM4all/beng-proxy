/*
 * Launch and manage "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_STOCK_HXX
#define BENG_PROXY_LHTTP_STOCK_HXX

#include "FdType.hxx"
#include "glibfwd.hxx"

#include <inline/compiler.h>

struct pool;
struct LhttpStock;
struct StockItem;
struct LhttpAddress;

LhttpStock *
lhttp_stock_new(struct pool *pool, unsigned limit, unsigned max_idle);

void
lhttp_stock_free(LhttpStock *lhttp_stock);

void
lhttp_stock_fade_all(LhttpStock &ls);

StockItem *
lhttp_stock_get(LhttpStock *lhttp_stock, struct pool *pool,
                const LhttpAddress *address,
                GError **error_r);

/**
 * Returns the socket descriptor of the specified stock item.
 */
gcc_pure
int
lhttp_stock_item_get_socket(const StockItem &item);

gcc_pure
FdType
lhttp_stock_item_get_type(const StockItem &item);

gcc_pure
const char *
lhttp_stock_item_get_name(const StockItem &item);

/**
 * Wrapper for hstock_put().
 */
void
lhttp_stock_put(LhttpStock *stock, StockItem &item, bool destroy);

#endif
