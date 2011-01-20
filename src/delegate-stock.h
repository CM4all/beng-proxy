/*
 * Delegate helper pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_STOCK_H
#define BENG_DELEGATE_STOCK_H

#include "stock.h"

struct jail_params;

struct hstock *
delegate_stock_new(pool_t pool);

void
delegate_stock_get(struct hstock *delegate_stock, pool_t pool,
                   const char *path, const char *document_root,
                   const struct jail_params *jail,
                   const struct stock_handler *handler, void *handler_ctx,
                   struct async_operation_ref *async_ref);

void
delegate_stock_put(struct hstock *delegate_stock,
                   struct stock_item *item, bool destroy);

int
delegate_stock_item_get(struct stock_item *item);

#endif
