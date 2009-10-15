/*
 * Anonymous pipe pooling, to speed to istream_pipe.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PIPE_STOCK_H
#define BENG_PROXY_PIPE_STOCK_H

#include "pool.h"

struct stock;
struct stock_item;

struct stock *
pipe_stock_new(pool_t pool);

void
pipe_stock_item_get(struct stock_item *item, int fds[2]);

#endif
