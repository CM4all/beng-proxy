/*
 * Launch and manage "Local HTTP" child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_STOCK_H
#define BENG_PROXY_LHTTP_STOCK_H

#include "istream-direct.h"

#include <inline/compiler.h>

#include <stdbool.h>

struct pool;
struct hstock;
struct stock_item;
struct stock_get_handler;
struct lhttp_address;
struct async_operation_ref;

struct hstock *
lhttp_stock_new(struct pool *pool, unsigned limit, unsigned max_idle);

void
lhttp_stock_get(struct hstock *hstock, struct pool *pool,
                const struct lhttp_address *address,
                const struct stock_get_handler *handler, void *handler_ctx,
                struct async_operation_ref *async_ref);

/**
 * Returns the socket descriptor of the specified stock item.
 */
gcc_pure
int
lhttp_stock_item_get_socket(const struct stock_item *item);

gcc_pure
enum istream_direct
lhttp_stock_item_get_type(const struct stock_item *item);

/**
 * Wrapper for hstock_put().
 */
void
lhttp_stock_put(struct hstock *hstock, struct stock_item *item, bool destroy);

#endif
