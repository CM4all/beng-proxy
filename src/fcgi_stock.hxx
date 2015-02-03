/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_STOCK_HXX
#define BENG_PROXY_FCGI_STOCK_HXX

#include "glibfwd.hxx"

struct pool;
struct StockItem;
struct StockGetHandler;
struct child_options;
struct async_operation_ref;
template<typename T> struct ConstBuffer;

struct fcgi_stock *
fcgi_stock_new(struct pool *pool, unsigned limit, unsigned max_idle);

void
fcgi_stock_free(struct fcgi_stock *fcgi_stock);

/**
 * @param args command-line arguments
 */
StockItem *
fcgi_stock_get(struct fcgi_stock *fcgi_stock, struct pool *pool,
               const struct child_options *options,
               const char *executable_path,
               ConstBuffer<const char *> args,
               ConstBuffer<const char *> env,
               GError **error_r);

/**
 * Returns the socket descriptor of the specified stock item.
 */
int
fcgi_stock_item_get(const StockItem &item);

int
fcgi_stock_item_get_domain(const StockItem &item);

/**
 * Translates a path into the application's namespace.
 */
const char *
fcgi_stock_translate_path(const StockItem &item,
                          const char *path, struct pool *pool);

/**
 * Wrapper for fcgi_stock_put().
 */
void
fcgi_stock_put(struct fcgi_stock *fcgi_stock, StockItem &item,
               bool destroy);

/**
 * Let the fcgi_stock know that the client is being aborted.  The
 * fcgi_stock may then figure out that the client process is faulty
 * and kill it at the next chance.  Note that this function will not
 * release the process - fcgi_stock_put() stil needs to be called
 * after this function.
 */
void
fcgi_stock_aborted(StockItem &item);

#endif
