/*
 * Wrapper for the tcp_stock class to support load balancing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcp-balancer.h"
#include "tcp-stock.h"
#include "stock.h"
#include "address-envelope.h"
#include "address-list.h"
#include "balancer.h"
#include "failure.h"

struct tcp_balancer {
    struct hstock *tcp_stock;

    struct balancer *balancer;
};

struct tcp_balancer_request {
    struct pool *pool;
    struct tcp_balancer *tcp_balancer;

    /**
     * The "sticky id" of the incoming HTTP request.
     */
    unsigned session_sticky;

    /**
     * The number of remaining connection attempts.  We give up when
     * we get an error and this attribute is already zero.
     */
    unsigned retries;

    const struct address_list *address_list;
    const struct address_envelope *current_address;

    const struct stock_handler *handler;
    void *handler_ctx;

    struct async_operation_ref *async_ref;
};

static const struct stock_handler tcp_balancer_stock_handler;

static void
tcp_balancer_next(struct tcp_balancer_request *request)
{
    request->current_address = balancer_get(request->tcp_balancer->balancer,
                                            request->address_list,
                                            request->session_sticky);

    tcp_stock_get(request->tcp_balancer->tcp_stock, request->pool,
                  NULL,
                  &request->current_address->address,
                  request->current_address->length,
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
    struct tcp_balancer_request *request = ctx;

    failure_remove(&request->current_address->address,
                   request->current_address->length);

    request->handler->ready(item, request->handler_ctx);
}

static void
tcp_balancer_stock_error(GError *error, void *ctx)
{
    struct tcp_balancer_request *request = ctx;

    failure_add(&request->current_address->address,
                request->current_address->length);

    if (request->retries-- > 0)
        /* try again, next address */
        tcp_balancer_next(request);
    else
        /* give up */
        request->handler->error(error, request->handler_ctx);
}

static const struct stock_handler tcp_balancer_stock_handler = {
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
    struct tcp_balancer *tcp_balancer = p_malloc(pool, sizeof(*tcp_balancer));
    tcp_balancer->tcp_stock = tcp_stock;
    tcp_balancer->balancer = balancer;
    return tcp_balancer;
}

void
tcp_balancer_get(struct tcp_balancer *tcp_balancer, struct pool *pool,
                 unsigned session_sticky,
                 const struct address_list *address_list,
                 const struct stock_handler *handler, void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    struct tcp_balancer_request *request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->tcp_balancer = tcp_balancer;
    request->session_sticky = session_sticky;

    if (address_list->size <= 1)
        request->retries = 0;
    else if (address_list->size == 2)
        request->retries = 1;
    else if (address_list->size == 3)
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
