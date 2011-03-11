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
struct stock_handler;
struct async_operation_ref;
struct address_list;

/**
 * Creates a new TCP connection stock.
 *
 * @param pool the memory pool
 * @param balancer the load balancer object
 * @param limit the maximum number of connections per host
 * @return the new TCP connections stock (this function cannot fail)
 */
struct hstock *
tcp_stock_new(pool_t pool, struct balancer *balancer, unsigned limit);

void
tcp_stock_get(struct hstock *tcp_stock, pool_t pool, const char *name,
              const struct address_list *address_list,
              const struct stock_handler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref);

void
tcp_stock_put(struct hstock *tcp_stock, struct stock_item *item, bool destroy);

int
tcp_stock_item_get(const struct stock_item *item);

int
tcp_stock_item_get_domain(const struct stock_item *item);

#endif
