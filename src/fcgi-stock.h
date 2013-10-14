/*
 * Launch and manage FastCGI child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FCGI_STOCK_H
#define __BENG_FCGI_STOCK_H

#include <glib.h>

#include <stdbool.h>

struct pool;
struct stock_item;
struct stock_get_handler;
struct jail_params;
struct async_operation_ref;

#ifdef __cplusplus
extern "C" {
#endif

struct fcgi_stock *
fcgi_stock_new(struct pool *pool, unsigned limit, unsigned max_idle);

void
fcgi_stock_free(struct fcgi_stock *fcgi_stock);

/**
 * @param account_id the customer account id (JailCGI)
 * @param site_id the customer site id (JailCGI)
 * @param host_name the UTS host name (JailCGI)
 */
struct stock_item *
fcgi_stock_get(struct fcgi_stock *fcgi_stock, struct pool *pool,
               const struct jail_params *jail,
               const char *executable_path,
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

#ifdef __cplusplus
}
#endif

#endif
