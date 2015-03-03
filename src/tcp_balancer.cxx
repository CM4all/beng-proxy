/*
 * Wrapper for the tcp_stock class to support load balancing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcp_balancer.hxx"
#include "tcp_stock.hxx"
#include "stock.hxx"
#include "address_list.hxx"
#include "balancer.hxx"
#include "failure.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"

#include <glib.h>

struct tcp_balancer {
    StockMap &tcp_stock;

    struct balancer &balancer;

    tcp_balancer(StockMap &_tcp_stock,
                 struct balancer &_balancer)
        :tcp_stock(_tcp_stock), balancer(_balancer) {}
};

struct tcp_balancer_request {
    struct pool &pool;
    struct tcp_balancer &tcp_balancer;

    const bool ip_transparent;
    const SocketAddress bind_address;

    /**
     * The "sticky id" of the incoming HTTP request.
     */
    const unsigned session_sticky;

    const unsigned timeout;

    /**
     * The number of remaining connection attempts.  We give up when
     * we get an error and this attribute is already zero.
     */
    unsigned retries;

    const AddressList &address_list;
    SocketAddress current_address;

    const StockGetHandler &handler;
    void *const handler_ctx;

    struct async_operation_ref &async_ref;

    tcp_balancer_request(struct pool &_pool,
                         struct tcp_balancer &_tcp_balancer,
                         bool _ip_transparent,
                         SocketAddress _bind_address,
                         unsigned _session_sticky,
                         unsigned _timeout,
                         const AddressList &_address_list,
                         const StockGetHandler &_handler, void *_handler_ctx,
                         struct async_operation_ref &_async_ref)
        :pool(_pool), tcp_balancer(_tcp_balancer),
         ip_transparent(_ip_transparent),
         bind_address(_bind_address),
         session_sticky(_session_sticky),
         timeout(_timeout),
         address_list(_address_list),
         handler(_handler), handler_ctx(_handler_ctx),
         async_ref(_async_ref) {}
};

static SocketAddress last_address;

extern const StockGetHandler tcp_balancer_stock_handler;

static void
tcp_balancer_next(struct tcp_balancer_request *request)
{
    const SocketAddress address =
        balancer_get(request->tcp_balancer.balancer,
                     request->address_list,
                     request->session_sticky);

    /* we need to copy this address because it may come from
       the balancer's cache, and the according cache item may be
       flushed at any time */
    const struct sockaddr *new_address = (const struct sockaddr *)
        p_memdup(&request->pool, address.GetAddress(), address.GetSize());
    request->current_address = { new_address, address.GetSize() };

    tcp_stock_get(&request->tcp_balancer.tcp_stock, &request->pool,
                  nullptr,
                  request->ip_transparent,
                  request->bind_address,
                  request->current_address,
                  request->timeout,
                  &tcp_balancer_stock_handler, request,
                  &request->async_ref);
}

/*
 * stock handler
 *
 */

static void
tcp_balancer_stock_ready(StockItem &item, void *ctx)
{
    struct tcp_balancer_request *request = (struct tcp_balancer_request *)ctx;

    last_address = request->current_address;

    failure_unset(request->current_address, FAILURE_FAILED);

    request->handler.ready(item, request->handler_ctx);
}

static void
tcp_balancer_stock_error(GError *error, void *ctx)
{
    struct tcp_balancer_request *request = (struct tcp_balancer_request *)ctx;

    failure_add(request->current_address);

    if (request->retries-- > 0) {
        /* try again, next address */
        g_error_free(error);

        tcp_balancer_next(request);
    } else
        /* give up */
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

struct tcp_balancer *
tcp_balancer_new(struct pool *pool, StockMap &tcp_stock,
                 struct balancer &balancer)
{
    return NewFromPool<struct tcp_balancer>(*pool, tcp_stock, balancer);
}

void
tcp_balancer_get(struct tcp_balancer *tcp_balancer, struct pool *pool,
                 bool ip_transparent,
                 SocketAddress bind_address,
                 unsigned session_sticky,
                 const AddressList *address_list,
                 unsigned timeout,
                 const StockGetHandler *handler, void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    auto request =
        NewFromPool<struct tcp_balancer_request>(*pool, *pool, *tcp_balancer,
                                                 ip_transparent, bind_address,
                                                 session_sticky, timeout,
                                                 *address_list,
                                                 *handler, handler_ctx,
                                                 *async_ref);

    if (address_list->GetSize() <= 1)
        request->retries = 0;
    else if (address_list->GetSize() == 2)
        request->retries = 1;
    else if (address_list->GetSize() == 3)
        request->retries = 2;
    else
        request->retries = 3;

    tcp_balancer_next(request);
}

void
tcp_balancer_put(struct tcp_balancer *tcp_balancer, StockItem &item,
                 bool destroy)
{
    tcp_stock_put(&tcp_balancer->tcp_stock, item, destroy);
}

SocketAddress
tcp_balancer_get_last()
{
    return last_address;
}
