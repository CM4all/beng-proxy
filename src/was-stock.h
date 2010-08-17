/*
 * Launch and manage WAS child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_STOCK_H
#define BENG_PROXY_WAS_STOCK_H

#include "stock.h"
#include "was-launch.h"

struct hstock *
was_stock_new(pool_t pool, unsigned limit);

void
was_stock_get(struct hstock *hstock, pool_t pool,
              const char *executable_path, const char *jail_path,
              stock_callback_t callback, void *callback_ctx,
              struct async_operation_ref *async_ref);

/**
 * Returns the socket descriptor of the specified stock item.
 */
const struct was_process *
was_stock_item_get(const struct stock_item *item);

/**
 * Wrapper for hstock_put().
 */
void
was_stock_put(struct hstock *hstock, struct stock_item *item, bool destroy);

#endif
