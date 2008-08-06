/*
 * AJP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_AJP_STOCK_H
#define __BENG_AJP_STOCK_H

#include "pool.h"

struct stock_item;
struct ajp_connection;

struct hstock *
ajp_stock_new(pool_t pool);

struct ajp_connection *
ajp_stock_item_get(struct stock_item *item);

#endif
