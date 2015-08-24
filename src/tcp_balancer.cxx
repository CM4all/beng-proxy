/*
 * Wrapper for the tcp_stock class to support load balancing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcp_balancer.hxx"
#include "tcp_stock.hxx"
#include "generic_balancer.hxx"
#include "stock.hxx"

struct TcpBalancer {
    StockMap &tcp_stock;

    struct balancer &balancer;

    TcpBalancer(StockMap &_tcp_stock,
                struct balancer &_balancer)
        :tcp_stock(_tcp_stock), balancer(_balancer) {}
};

struct TcpBalancerRequest {
    TcpBalancer &tcp_balancer;

    const bool ip_transparent;
    const SocketAddress bind_address;

    const unsigned timeout;

    const StockGetHandler &handler;
    void *const handler_ctx;

    TcpBalancerRequest(TcpBalancer &_tcp_balancer,
                       bool _ip_transparent,
                       SocketAddress _bind_address,
                       unsigned _timeout,
                       const StockGetHandler &_handler, void *_handler_ctx)
        :tcp_balancer(_tcp_balancer),
         ip_transparent(_ip_transparent),
         bind_address(_bind_address),
         timeout(_timeout),
         handler(_handler), handler_ctx(_handler_ctx) {}

    void Send(struct pool &pool, SocketAddress address,
              struct async_operation_ref &async_ref);
};

static SocketAddress last_address;

extern const StockGetHandler tcp_balancer_stock_handler;

inline void
TcpBalancerRequest::Send(struct pool &pool, SocketAddress address,
                         struct async_operation_ref &async_ref)
{
    tcp_stock_get(&tcp_balancer.tcp_stock, &pool,
                  nullptr,
                  ip_transparent,
                  bind_address,
                  address,
                  timeout,
                  &tcp_balancer_stock_handler, this,
                  &async_ref);
}

/*
 * stock handler
 *
 */

static void
tcp_balancer_stock_ready(StockItem &item, void *ctx)
{
    auto request = (TcpBalancerRequest *)ctx;

    auto &base = BalancerRequest<TcpBalancerRequest>::Cast(*request);
    last_address = base.GetAddress();
    base.Success();

    request->handler.ready(item, request->handler_ctx);
}

static void
tcp_balancer_stock_error(GError *error, void *ctx)
{
    auto request = (TcpBalancerRequest *)ctx;

    auto &base = BalancerRequest<TcpBalancerRequest>::Cast(*request);
    if (!base.Failure())
        request->handler.error(error, request->handler_ctx);
}

const StockGetHandler tcp_balancer_stock_handler = {
    .ready = tcp_balancer_stock_ready,
    .error = tcp_balancer_stock_error,
};

/*
 * constructor
 *
 */

TcpBalancer *
tcp_balancer_new(StockMap &tcp_stock, struct balancer &balancer)
{
    return new TcpBalancer(tcp_stock, balancer);
}

void
tcp_balancer_free(TcpBalancer *tcp_balancer)
{
    delete tcp_balancer;
}

void
tcp_balancer_get(TcpBalancer &tcp_balancer, struct pool &pool,
                 bool ip_transparent,
                 SocketAddress bind_address,
                 unsigned session_sticky,
                 const AddressList &address_list,
                 unsigned timeout,
                 const StockGetHandler &handler, void *handler_ctx,
                 struct async_operation_ref &async_ref)
{
    BalancerRequest<TcpBalancerRequest>::Start(pool, tcp_balancer.balancer,
                                                        address_list, async_ref,
                                                        session_sticky,
                                                        tcp_balancer,
                                                        ip_transparent,
                                                        bind_address, timeout,
                                                        handler, handler_ctx);
}

void
tcp_balancer_put(TcpBalancer &tcp_balancer, StockItem &item,
                 bool destroy)
{
    tcp_stock_put(&tcp_balancer.tcp_stock, item, destroy);
}

SocketAddress
tcp_balancer_get_last()
{
    return last_address;
}
