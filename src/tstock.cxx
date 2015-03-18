/*
 * Connection pooling for the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tstock.hxx"
#include "translate_client.hxx"
#include "stock.hxx"
#include "tcp_stock.hxx"
#include "lease.hxx"
#include "pool.hxx"
#include "net/AllocatedSocketAddress.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <sys/un.h>
#include <sys/socket.h>

struct tstock {
    StockMap &tcp_stock;

    AllocatedSocketAddress address;

    const char *const address_string;

    tstock(StockMap &_tcp_stock, const char *path)
        :tcp_stock(_tcp_stock), address_string(path) {
        address.SetLocal(path);
    }
};

struct tstock_request {
    struct pool &pool;

    struct tstock &stock;
    StockItem *item;

    const TranslateRequest &request;

    const TranslateHandler &handler;
    void *handler_ctx;

    struct async_operation_ref &async_ref;

    tstock_request(struct tstock &_stock, struct pool &_pool,
                   const TranslateRequest &_request,
                   const TranslateHandler &_handler, void *_ctx,
                   struct async_operation_ref &_async_ref)
        :pool(_pool), stock(_stock),
         request(_request),
         handler(_handler), handler_ctx(_ctx),
         async_ref(_async_ref) {}
};


/*
 * socket lease
 *
 */

static void
tstock_socket_release(bool reuse, void *ctx)
{
    tstock_request *r = (tstock_request *)ctx;

    tcp_stock_put(&r->stock.tcp_stock, *r->item, !reuse);
}

static const struct lease tstock_socket_lease = {
    .release = tstock_socket_release,
};


/*
 * stock callback
 *
 */

static void
tstock_stock_ready(StockItem &item, void *ctx)
{
    tstock_request *r = (tstock_request *)ctx;

    r->item = &item;
    translate(&r->pool, tcp_stock_item_get(item),
              &tstock_socket_lease, r,
              &r->request, &r->handler, r->handler_ctx,
              &r->async_ref);
}

static void
tstock_stock_error(GError *error, void *ctx)
{
    tstock_request *r = (tstock_request *)ctx;

    r->handler.error(error, r->handler_ctx);
}

static constexpr StockGetHandler tstock_stock_handler = {
    .ready = tstock_stock_ready,
    .error = tstock_stock_error,
};


/*
 * constructor
 *
 */

struct tstock *
tstock_new(struct pool &pool, StockMap &tcp_stock, const char *socket_path)
{
    return NewFromPool<tstock>(pool, tcp_stock, socket_path);
}

void
tstock_free(struct pool &pool, struct tstock *stock)
{
    DeleteFromPool(pool, stock);
}

void
tstock_translate(struct tstock &stock, struct pool &pool,
                 const TranslateRequest &request,
                 const TranslateHandler &handler, void *ctx,
                 struct async_operation_ref &async_ref)
{
    auto r = NewFromPool<tstock_request>(pool, stock, pool, request,
                                         handler, ctx, async_ref);

    tcp_stock_get(&stock.tcp_stock, &pool, stock.address_string,
                  false, SocketAddress::Null(),
                  stock.address,
                  10,
                  &tstock_stock_handler, r,
                  &async_ref);
}
