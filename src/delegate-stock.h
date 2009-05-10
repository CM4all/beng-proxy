/*
 * Delegate helper pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_DELEGATE_STOCK_H
#define BENG_DELEGATE_STOCK_H

#include "pool.h"

struct stock_item;

struct hstock *
delegate_stock_new(pool_t pool);

int
delegate_stock_item_get(struct stock_item *item);

#endif
