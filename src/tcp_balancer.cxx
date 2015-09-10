/*
 * Wrapper for the tcp_stock class to support load balancing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcp_balancer.hxx"
#include "tcp_stock.hxx"
#include "generic_balancer.hxx"
#include "stock/Stock.hxx"

struct TcpBalancer {
    StockMap &tcp_stock;

    struct balancer &balancer;

    TcpBalancer(StockMap &_tcp_stock,
                struct balancer &_balancer)
        :tcp_stock(_tcp_stock), balancer(_balancer) {}
};

struct TcpBalancerRequest : public StockGetHandler {
    TcpBalancer &tcp_balancer;

    const bool ip_transparent;
    const SocketAddress bind_address;

    const unsigned timeout;

    StockGetHandler &handler;

    TcpBalancerRequest(TcpBalancer &_tcp_balancer,
                       bool _ip_transparent,
                       SocketAddress _bind_address,
                       unsigned _timeout,
                       StockGetHandler &_handler)
        :tcp_balancer(_tcp_balancer),
         ip_transparent(_ip_transparent),
         bind_address(_bind_address),
         timeout(_timeout),
         handler(_handler) {}

    void Send(struct pool &pool, SocketAddress address,
              struct async_operation_ref &async_ref);

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;
};

static SocketAddress last_address;

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
                  *this,
                  async_ref);
}

/*
 * stock handler
 *
 */

void
TcpBalancerRequest::OnStockItemReady(StockItem &item)
{
    auto &base = BalancerRequest<TcpBalancerRequest>::Cast(*this);
    last_address = base.GetAddress();
    base.Success();

    handler.OnStockItemReady(item);
}

void
TcpBalancerRequest::OnStockItemError(GError *error)
{
    auto &base = BalancerRequest<TcpBalancerRequest>::Cast(*this);
    if (!base.Failure())
        handler.OnStockItemError(error);
}

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
                 StockGetHandler &handler,
                 struct async_operation_ref &async_ref)
{
    BalancerRequest<TcpBalancerRequest>::Start(pool, tcp_balancer.balancer,
                                                        address_list, async_ref,
                                                        session_sticky,
                                                        tcp_balancer,
                                                        ip_transparent,
                                                        bind_address, timeout,
                                                        handler);
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
