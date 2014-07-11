/*
 * Wrapper for the tcp_stock class to support load balancing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcp_balancer.hxx"
#include "tcp_stock.hxx"
#include "stock.hxx"
#include "address_envelope.hxx"
#include "address_list.hxx"
#include "balancer.hxx"
#include "failure.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"

#include <glib.h>

struct tcp_balancer {
    struct hstock *tcp_stock;

    struct balancer *balancer;
};

struct tcp_balancer_request {
    struct pool *pool;
    struct tcp_balancer *tcp_balancer;

    bool ip_transparent;
    SocketAddress bind_address;

    /**
     * The "sticky id" of the incoming HTTP request.
     */
    unsigned session_sticky;

    unsigned timeout;

    /**
     * The number of remaining connection attempts.  We give up when
     * we get an error and this attribute is already zero.
     */
    unsigned retries;

    const struct address_list *address_list;
    const struct address_envelope *current_address;

    const struct stock_get_handler *handler;
    void *handler_ctx;

    struct async_operation_ref *async_ref;
};

static const struct address_envelope *last_address;

extern const struct stock_get_handler tcp_balancer_stock_handler;

static void
tcp_balancer_next(struct tcp_balancer_request *request)
{
    const struct address_envelope &envelope =
        balancer_get(*request->tcp_balancer->balancer,
                     *request->address_list,
                     request->session_sticky);

    /* we need to copy this address_envelope because it may come from
       the balancer's cache, and the according cache item may be
       flushed at any time */
    request->current_address = (const struct address_envelope *)
        p_memdup(request->pool, &envelope,
                 sizeof(envelope)
                 - sizeof(envelope.address)
                 + envelope.length);

    tcp_stock_get(request->tcp_balancer->tcp_stock, request->pool,
                  nullptr,
                  request->ip_transparent,
                  request->bind_address,
                  SocketAddress(&request->current_address->address,
                                request->current_address->length),
                  request->timeout,
                  &tcp_balancer_stock_handler, request,
                  request->async_ref);
}

/*
 * stock handler
 *
 */

static void
tcp_balancer_stock_ready(struct stock_item *item, void *ctx)
{
    struct tcp_balancer_request *request = (struct tcp_balancer_request *)ctx;

    last_address = request->current_address;

    failure_unset(&request->current_address->address,
                  request->current_address->length,
                  FAILURE_FAILED);

    request->handler->ready(item, request->handler_ctx);
}

static void
tcp_balancer_stock_error(GError *error, void *ctx)
{
    struct tcp_balancer_request *request = (struct tcp_balancer_request *)ctx;

    failure_add(&request->current_address->address,
                request->current_address->length);

    if (request->retries-- > 0) {
        /* try again, next address */
        g_error_free(error);

        tcp_balancer_next(request);
    } else
        /* give up */
        request->handler->error(error, request->handler_ctx);
}

const struct stock_get_handler tcp_balancer_stock_handler = {
    .ready = tcp_balancer_stock_ready,
    .error = tcp_balancer_stock_error,
};

/*
 * constructor
 *
 */

struct tcp_balancer *
tcp_balancer_new(struct pool *pool, struct hstock *tcp_stock,
                 struct balancer *balancer)
{
    auto tcp_balancer = NewFromPool<struct tcp_balancer>(*pool);
    tcp_balancer->tcp_stock = tcp_stock;
    tcp_balancer->balancer = balancer;
    return tcp_balancer;
}

void
tcp_balancer_get(struct tcp_balancer *tcp_balancer, struct pool *pool,
                 bool ip_transparent,
                 SocketAddress bind_address,
                 unsigned session_sticky,
                 const struct address_list *address_list,
                 unsigned timeout,
                 const struct stock_get_handler *handler, void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    auto request = NewFromPool<struct tcp_balancer_request>(*pool);
    request->pool = pool;
    request->tcp_balancer = tcp_balancer;
    request->ip_transparent = ip_transparent;
    request->bind_address = bind_address;
    request->session_sticky = session_sticky;
    request->timeout = timeout;

    if (address_list->GetSize() <= 1)
        request->retries = 0;
    else if (address_list->GetSize() == 2)
        request->retries = 1;
    else if (address_list->GetSize() == 3)
        request->retries = 2;
    else
        request->retries = 3;

    request->address_list = address_list;
    request->handler = handler;
    request->handler_ctx = handler_ctx;
    request->async_ref = async_ref;

    tcp_balancer_next(request);
}

void
tcp_balancer_put(struct tcp_balancer *tcp_balancer, struct stock_item *item,
                 bool destroy)
{
    tcp_stock_put(tcp_balancer->tcp_stock, item, destroy);
}

const struct address_envelope *
tcp_balancer_get_last(void)
{
    return last_address;
}
