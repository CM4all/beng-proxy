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
struct FcgiStock;
struct ChildOptions;
struct async_operation_ref;
template<typename T> struct ConstBuffer;

FcgiStock *
fcgi_stock_new(struct pool *pool, unsigned limit, unsigned max_idle);

void
fcgi_stock_free(FcgiStock *fcgi_stock);

void
fcgi_stock_fade_all(FcgiStock &fs);

/**
 * @param args command-line arguments
 */
StockItem *
fcgi_stock_get(FcgiStock *fcgi_stock, struct pool *pool,
               const ChildOptions &options,
               const char *executable_path,
               ConstBuffer<const char *> args,
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
 * Let the fcgi_stock know that the client is being aborted.  The
 * fcgi_stock may then figure out that the client process is faulty
 * and kill it at the next chance.  Note that this function will not
 * release the process - fcgi_stock_put() stil needs to be called
 * after this function.
 */
void
fcgi_stock_aborted(StockItem &item);

#endif
