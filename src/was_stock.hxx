/*
 * Launch and manage WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_STOCK_HXX
#define BENG_PROXY_WAS_STOCK_HXX

#include "was_launch.hxx"

#include <inline/compiler.h>

struct pool;
struct StockMap;
struct StockItem;
struct StockGetHandler;
struct async_operation_ref;
template<typename T> struct ConstBuffer;

StockMap *
was_stock_new(struct pool *pool, unsigned limit, unsigned max_idle);

/**
 * @param args command-line arguments
 */
void
was_stock_get(StockMap *hstock, struct pool *pool,
              const struct child_options *options,
              const char *executable_path,
              ConstBuffer<const char *> args,
              ConstBuffer<const char *> env,
              const StockGetHandler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref);

/**
 * Returns the socket descriptor of the specified stock item.
 */
gcc_pure
const struct was_process &
was_stock_item_get(const StockItem &item);

/**
 * Translates a path into the application's namespace.
 */
gcc_pure
const char *
was_stock_translate_path(const StockItem &item,
                         const char *path, struct pool *pool);

/**
 * Wrapper for hstock_put().
 */
void
was_stock_put(StockMap *hstock, StockItem &item, bool destroy);

#endif
