/*
 * HTTP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_STOCK_H
#define __BENG_HTTP_STOCK_H

#include "pool.h"

struct stock_item;
struct http_client_connection;

struct hstock *
http_stock_new(pool_t pool);

struct http_client_connection *
http_stock_item_get(struct stock_item *item);

#endif
