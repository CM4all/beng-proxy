/*
 * TCP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TCP_STOCK_HXX
#define BENG_PROXY_TCP_STOCK_HXX

#include <inline/compiler.h>

struct pool;
struct balancer;
class StockMap;
struct StockItem;
class SocketDescriptor;
class StockGetHandler;
class CancellablePointer;
class SocketAddress;
class EventLoop;

/**
 * Creates a new TCP connection stock.
 *
 * @param limit the maximum number of connections per host
 * @return the new TCP connections stock (this function cannot fail)
 */
StockMap *
tcp_stock_new(EventLoop &event_loop, unsigned limit);

/**
 * @param name the MapStock name; it is auto-generated from the
 * #address if nullptr is passed here
 * @param timeout the connect timeout in seconds
 */
void
tcp_stock_get(StockMap *tcp_stock, struct pool *pool, const char *name,
              bool ip_transparent,
              SocketAddress bind_address,
              SocketAddress address,
              unsigned timeout,
              StockGetHandler &handler,
              CancellablePointer &cancel_ptr);

gcc_pure
SocketDescriptor
tcp_stock_item_get(const StockItem &item);

/**
 * Returns the (peer) address this object is connected to.
 */
gcc_pure
SocketAddress
tcp_stock_item_get_address(const StockItem &item);

gcc_pure
int
tcp_stock_item_get_domain(const StockItem &item);

#endif
