/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tstock.hxx"
#include "translate_client.hxx"
#include "stock.hxx"
#include "tcp_stock.hxx"
#include "lease.h"
#include "pool.hxx"
#include "net/SocketAddress.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <sys/un.h>
#include <sys/socket.h>

struct tstock {
    struct hstock *tcp_stock;

    struct sockaddr_un address;
    socklen_t address_size;

    const char *address_string;
};

struct tstock_request {
    struct pool *pool;

    struct tstock *stock;
    struct stock_item *item;

    const TranslateRequest *request;

    const TranslateHandler *handler;
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
    tstock_request *r = (tstock_request *)ctx;

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
    tstock_request *r = (tstock_request *)ctx;

    r->item = item;
    translate(r->pool, tcp_stock_item_get(item),
              &tstock_socket_lease, r,
              r->request, r->handler, r->handler_ctx,
              r->async_ref);
}

static void
tstock_stock_error(GError *error, void *ctx)
{
    tstock_request *r = (tstock_request *)ctx;

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
    auto stock = NewFromPool<tstock>(*pool);

    assert(tcp_stock != nullptr);
    assert(socket_path != nullptr);

    stock->tcp_stock = tcp_stock;

    size_t socket_path_length = strlen(socket_path);
    if (socket_path_length >= sizeof(stock->address.sun_path))
        socket_path_length = sizeof(stock->address.sun_path) - 1;

    stock->address.sun_family = AF_UNIX;
    memcpy(stock->address.sun_path, socket_path, socket_path_length);
    stock->address.sun_path[socket_path_length] = 0;

    stock->address_size = SUN_LEN(&stock->address);

    if (socket_path[0] == '@')
        stock->address.sun_path[0] = 0;

    stock->address_string = socket_path;

    return stock;
}

void
tstock_translate(struct tstock *stock, struct pool *pool,
                 const TranslateRequest *request,
                 const TranslateHandler *handler, void *ctx,
                 struct async_operation_ref *async_ref)
{
    auto r = NewFromPool<tstock_request>(*pool);

    r->pool = pool;
    r->stock = stock;
    r->request = request;
    r->handler = handler;
    r->handler_ctx = ctx;
    r->async_ref = async_ref;

    tcp_stock_get(stock->tcp_stock, pool, stock->address_string,
                  false, SocketAddress::Null(),
                  { (const struct sockaddr *)&stock->address, stock->address_size },
                  10,
                  &tstock_stock_handler, r,
                  async_ref);
}
