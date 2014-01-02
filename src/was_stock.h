/*
 * Launch and manage WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_STOCK_H
#define BENG_PROXY_WAS_STOCK_H

#include "was_launch.h"

#include <inline/compiler.h>

struct pool;
struct hstock;
struct stock_item;
struct stock_get_handler;
struct async_operation_ref;

#ifdef __cplusplus
extern "C" {
#endif

struct hstock *
was_stock_new(struct pool *pool, unsigned limit, unsigned max_idle);

/**
 * @param args command-line arguments
 */
void
was_stock_get(struct hstock *hstock, struct pool *pool,
              const struct child_options *options,
              const char *executable_path,
              const char *const*args, unsigned n_args,
              const struct stock_get_handler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref);

/**
 * Returns the socket descriptor of the specified stock item.
 */
gcc_pure
const struct was_process *
was_stock_item_get(const struct stock_item *item);

/**
 * Translates a path into the application's namespace.
 */
gcc_pure
const char *
was_stock_translate_path(const struct stock_item *item,
                         const char *path, struct pool *pool);

/**
 * Wrapper for hstock_put().
 */
void
was_stock_put(struct hstock *hstock, struct stock_item *item, bool destroy);

#ifdef __cplusplus
}
#endif

#endif
