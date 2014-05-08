/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_STOCK_HXX
#define BENG_PROXY_FCGI_STOCK_HXX

#include <glib.h>

struct pool;
struct stock_item;
struct stock_get_handler;
struct child_options;
struct async_operation_ref;

struct fcgi_stock *
fcgi_stock_new(struct pool *pool, unsigned limit, unsigned max_idle);

void
fcgi_stock_free(struct fcgi_stock *fcgi_stock);

/**
 * @param args command-line arguments
 */
struct stock_item *
fcgi_stock_get(struct fcgi_stock *fcgi_stock, struct pool *pool,
               const struct child_options *options,
               const char *executable_path,
               const char *const*args, unsigned n_args,
               GError **error_r);

/**
 * Returns the socket descriptor of the specified stock item.
 */
int
fcgi_stock_item_get(const struct stock_item *item);

int
fcgi_stock_item_get_domain(const struct stock_item *item);

/**
 * Translates a path into the application's namespace.
 */
const char *
fcgi_stock_translate_path(const struct stock_item *item,
                          const char *path, struct pool *pool);

/**
 * Wrapper for fcgi_stock_put().
 */
void
fcgi_stock_put(struct fcgi_stock *fcgi_stock, struct stock_item *item,
               bool destroy);

/**
 * Let the fcgi_stock know that the client is being aborted.  The
 * fcgi_stock may then figure out that the client process is faulty
 * and kill it at the next chance.  Note that this function will not
 * release the process - fcgi_stock_put() stil needs to be called
 * after this function.
 */
void
fcgi_stock_aborted(struct stock_item *item);

#endif
