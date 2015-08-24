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

struct TranslateStockRequest final : public StockGetHandler {
    struct pool &pool;

    struct tstock &stock;
    StockItem *item;

    const TranslateRequest &request;

    const TranslateHandler &handler;
    void *handler_ctx;

    struct async_operation_ref &async_ref;

    TranslateStockRequest(struct tstock &_stock, struct pool &_pool,
                          const TranslateRequest &_request,
                          const TranslateHandler &_handler, void *_ctx,
                          struct async_operation_ref &_async_ref)
        :pool(_pool), stock(_stock),
         request(_request),
         handler(_handler), handler_ctx(_ctx),
         async_ref(_async_ref) {}

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;
};


/*
 * socket lease
 *
 */

static void
tstock_socket_release(bool reuse, void *ctx)
{
    TranslateStockRequest *r = (TranslateStockRequest *)ctx;

    tcp_stock_put(&r->stock.tcp_stock, *r->item, !reuse);
}

static const struct lease tstock_socket_lease = {
    .release = tstock_socket_release,
};


/*
 * stock callback
 *
 */

void
TranslateStockRequest::OnStockItemReady(StockItem &_item)
{
    item = &_item;
    translate(pool, tcp_stock_item_get(_item),
              tstock_socket_lease, this,
              request, handler, handler_ctx,
              async_ref);
}

void
TranslateStockRequest::OnStockItemError(GError *error)
{
    handler.error(error, handler_ctx);
}

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
    auto r = NewFromPool<TranslateStockRequest>(pool, stock, pool, request,
                                                handler, ctx, async_ref);

    tcp_stock_get(&stock.tcp_stock, &pool, stock.address_string,
                  false, SocketAddress::Null(),
                  stock.address,
                  10,
                  *r, async_ref);
}
