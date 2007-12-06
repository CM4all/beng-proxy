/*
 * HTTP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_URL_STOCK_H
#define __BENG_URL_STOCK_H

#include "pool.h"
#include "http-client.h"

struct stock_item;

struct hstock *
url_hstock_new(pool_t pool);

http_client_connection_t
url_stock_item_get(struct stock_item *item);

#endif
