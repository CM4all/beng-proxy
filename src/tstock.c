/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tstock.h"
#include "stock.h"
#include "tcp-stock.h"
#include "lease.h"

#include <assert.h>

struct tstock {
    struct hstock *tcp_stock;

    const char *socket_path;
};

struct tstock_request {
    pool_t pool;

    struct tstock *stock;
    struct stock_item *item;

    const struct translate_request *request;

    translate_callback_t callback;
    void *callback_ctx;

    struct async_operation_ref *async_ref;
};

static const struct translate_response error = {
    .status = -1,
};


/*
 * socket lease
 *
 */

static void
tstock_socket_release(bool reuse, void *ctx)
{
    struct tstock_request *r = ctx;

    hstock_put(r->stock->tcp_stock, r->stock->socket_path,
               r->item, !reuse);
}

static const struct lease tstock_socket_lease = {
    .release = tstock_socket_release,
};


/*
 * stock callback
 *
 */

static void
tstock_stock_callback(void *ctx, struct stock_item *item)
{
    struct tstock_request *r = ctx;

    if (item == NULL) {
        r->callback(&error, r->callback_ctx);
        return;
    }

    r->item = item;
    translate(r->pool, tcp_stock_item_get(item),
              &tstock_socket_lease, r,
              r->request, r->callback, r->callback_ctx,
              r->async_ref);
}


/*
 * constructor
 *
 */

struct tstock *
tstock_new(pool_t pool, struct hstock *tcp_stock, const char *socket_path)
{
    struct tstock *stock = p_malloc(pool, sizeof(*stock));

    assert(tcp_stock != NULL);
    assert(socket_path != NULL);

    stock->tcp_stock = tcp_stock;
    stock->socket_path = socket_path;

    return stock;
}

void
tstock_translate(struct tstock *stock, pool_t pool,
                 const struct translate_request *request,
                 translate_callback_t callback, void *ctx,
                 struct async_operation_ref *async_ref)
{
    struct tstock_request *r = p_malloc(pool, sizeof(*r));

    r->pool = pool;
    r->stock = stock;
    r->request = request;
    r->callback = callback;
    r->callback_ctx = ctx;
    r->async_ref = async_ref;

    hstock_get(stock->tcp_stock, pool, stock->socket_path, NULL,
               tstock_stock_callback, r,
               async_ref);
}
