/*
 * Wrapper for the tcp_stock class to support load balancing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TCP_BALANCER_HXX
#define BENG_PROXY_TCP_BALANCER_HXX

#include <inline/compiler.h>

struct pool;
struct balancer;
struct AddressList;
struct StockMap;
class StockGetHandler;
struct StockItem;
struct async_operation_ref;
class SocketAddress;

struct TcpBalancer;

/**
 * Creates a new TCP connection stock.
 *
 * @param tcp_stock the underlying tcp_stock object
 * @param balancer the load balancer object
 * @return the new TCP connections stock (this function cannot fail)
 */
TcpBalancer *
tcp_balancer_new(StockMap &tcp_stock, struct balancer &balancer);

void
tcp_balancer_free(TcpBalancer *tcp_balancer);

/**
 * @param session_sticky a portion of the session id that is used to
 * select the worker; 0 means disable stickiness
 * @param timeout the connect timeout for each attempt [seconds]
 */
void
tcp_balancer_get(TcpBalancer &tcp_balancer, struct pool &pool,
                 bool ip_transparent,
                 SocketAddress bind_address,
                 unsigned session_sticky,
                 const AddressList &address_list,
                 unsigned timeout,
                 StockGetHandler &handler,
                 struct async_operation_ref &async_ref);

/**
 * Returns the address of the last connection that was established
 * successfully.  This is a dirty hack to allow the #tcp_stock's
 * #stock_handler to find this out.
 */
gcc_pure
SocketAddress
tcp_balancer_get_last();

#endif
