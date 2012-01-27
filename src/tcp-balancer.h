/*
 * Wrapper for the tcp_stock class to support load balancing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TCP_STOCK_H
#define BENG_PROXY_TCP_STOCK_H

#include <stdbool.h>

struct hstock;
struct pool;
struct balancer;
struct address_list;
struct stock_get_handler;
struct stock_item;
struct async_operation_ref;

struct tcp_balancer;

/**
 * Creates a new TCP connection stock.
 *
 * @param pool the memory pool
 * @param tcp_stock the underlying tcp_stock object
 * @param balancer the load balancer object
 * @param limit the maximum number of connections per host
 * @return the new TCP connections stock (this function cannot fail)
 */
struct tcp_balancer *
tcp_balancer_new(struct pool *pool, struct hstock *tcp_stock,
                 struct balancer *balancer);

/**
 * @param session_sticky a portion of the session id that is used to
 * select the worker; 0 means disable stickiness
 */
void
tcp_balancer_get(struct tcp_balancer *tcp_balancer, struct pool *pool,
                 unsigned session_sticky,
                 const struct address_list *address_list,
                 const struct stock_get_handler *handler, void *handler_ctx,
                 struct async_operation_ref *async_ref);

void
tcp_balancer_put(struct tcp_balancer *tcp_balancer, struct stock_item *item,
                 bool destroy);

/**
 * Returns the address of the last connection that was established
 * successfully.  This is a dirty hack to allow the #tcp_stock's
 * #stock_handler to find this out.
 */
const struct address_envelope *
tcp_balancer_get_last(void);

#endif
