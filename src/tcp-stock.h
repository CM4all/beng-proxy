/*
 * TCP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TCP_STOCK_H
#define __BENG_TCP_STOCK_H

#include "pool.h"

struct balancer;
struct stock_item;

struct hstock *
tcp_stock_new(pool_t pool, struct balancer *balancer);

int
tcp_stock_item_get(struct stock_item *item);

#endif
