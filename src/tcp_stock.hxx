/*
 * TCP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TCP_STOCK_HXX
#define BENG_PROXY_TCP_STOCK_HXX

struct pool;
struct balancer;
struct StockItem;
struct StockGetHandler;
struct async_operation_ref;
class SocketAddress;

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
 * @param timeout the connect timeout in seconds
 */
void
tcp_stock_get(struct hstock *tcp_stock, struct pool *pool, const char *name,
              bool ip_transparent,
              SocketAddress bind_address,
              SocketAddress address,
              unsigned timeout,
              const StockGetHandler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref);

void
tcp_stock_put(struct hstock *tcp_stock, StockItem *item, bool destroy);

int
tcp_stock_item_get(const StockItem *item);

int
tcp_stock_item_get_domain(const StockItem *item);

#endif
