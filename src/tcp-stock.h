/*
 * TCP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TCP_STOCK_H
#define __BENG_TCP_STOCK_H

#include <stdbool.h>
#include <stddef.h>

struct pool;
struct balancer;
struct stock_item;
struct stock_handler;
struct async_operation_ref;
struct sockaddr;

/**
 * Creates a new TCP connection stock.
 *
 * @param pool the memory pool
 * @param limit the maximum number of connections per host
 * @return the new TCP connections stock (this function cannot fail)
 */
struct hstock *
tcp_stock_new(struct pool *pool, unsigned limit);

/**
 * @param name the hstock name; it is auto-generated from the
 * #address_list if NULL is passed here
 */
void
tcp_stock_get(struct hstock *tcp_stock, struct pool *pool, const char *name,
              const struct sockaddr *address, size_t address_length,
              const struct stock_handler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref);

void
tcp_stock_put(struct hstock *tcp_stock, struct stock_item *item, bool destroy);

int
tcp_stock_item_get(const struct stock_item *item);

int
tcp_stock_item_get_domain(const struct stock_item *item);

#endif
