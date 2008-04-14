/*
 * HTTP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URL_STOCK_H
#define __BENG_URL_STOCK_H

#include "pool.h"

struct stock_item;
struct http_client_connection;

struct hstock *
url_hstock_new(pool_t pool);

struct http_client_connection *
url_stock_item_get(struct stock_item *item);

#endif
