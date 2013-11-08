/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tstock.h"
#include "translate-client.h"
#include "stock.h"
#include "tcp-stock.h"
#include "lease.h"

#include <daemon/log.h>

#include <assert.h>
#include <sys/un.h>
#include <sys/socket.h>

struct tstock {
    struct hstock *tcp_stock;

    struct sockaddr_un address;
};

struct tstock_request {
    struct pool *pool;

    struct tstock *stock;
    struct stock_item *item;

    const struct translate_request *request;

    const struct translate_handler *handler;
    void *handler_ctx;

    struct async_operation_ref *async_ref;
};


/*
 * socket lease
 *
 */

static void
tstock_socket_release(bool reuse, void *ctx)
{
    struct tstock_request *r = ctx;

    tcp_stock_put(r->stock->tcp_stock, r->item, !reuse);
}

static const struct lease tstock_socket_lease = {
    .release = tstock_socket_release,
};


/*
 * stock callback
 *
 */

static void
tstock_stock_ready(struct stock_item *item, void *ctx)
{
    struct tstock_request *r = ctx;

    r->item = item;
    translate(r->pool, tcp_stock_item_get(item),
              &tstock_socket_lease, r,
              r->request, r->handler, r->handler_ctx,
              r->async_ref);
}

static void
tstock_stock_error(GError *error, void *ctx)
{
    struct tstock_request *r = ctx;

    r->handler->error(error, r->handler_ctx);
}

static const struct stock_get_handler tstock_stock_handler = {
    .ready = tstock_stock_ready,
    .error = tstock_stock_error,
};


/*
 * constructor
 *
 */

struct tstock *
tstock_new(struct pool *pool, struct hstock *tcp_stock, const char *socket_path)
{
    struct tstock *stock = p_malloc(pool, sizeof(*stock));

    assert(tcp_stock != NULL);
    assert(socket_path != NULL);

    stock->tcp_stock = tcp_stock;

    size_t socket_path_length = strlen(socket_path);
    if (socket_path_length >= sizeof(stock->address.sun_path))
        socket_path_length = sizeof(stock->address.sun_path) - 1;

    stock->address.sun_family = AF_UNIX;
    memcpy(stock->address.sun_path, socket_path, socket_path_length);
    stock->address.sun_path[socket_path_length] = 0;

    return stock;
}

void
tstock_translate(struct tstock *stock, struct pool *pool,
                 const struct translate_request *request,
                 const struct translate_handler *handler, void *ctx,
                 struct async_operation_ref *async_ref)
{
    struct tstock_request *r = p_malloc(pool, sizeof(*r));

    r->pool = pool;
    r->stock = stock;
    r->request = request;
    r->handler = handler;
    r->handler_ctx = ctx;
    r->async_ref = async_ref;

    tcp_stock_get(stock->tcp_stock, pool, stock->address.sun_path,
                  false, NULL, 0,
                  (const struct sockaddr *)&stock->address,
                  SUN_LEN(&stock->address),
                  10,
                  &tstock_stock_handler, r,
                  async_ref);
}
