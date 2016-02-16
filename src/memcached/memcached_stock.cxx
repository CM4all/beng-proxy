/*
 * Stock of connections to a memcached server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "memcached_stock.hxx"
#include "memcached_client.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "address_list.hxx"
#include "lease.hxx"
#include "pool.hxx"
#include "istream/istream.hxx"
#include "net/SocketAddress.hxx"

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <sys/socket.h>

struct memcached_stock {
    TcpBalancer *tcp_balancer;

    const AddressList *address;
};

struct memcached_stock *
memcached_stock_new(struct pool &pool, TcpBalancer *tcp_balancer,
                    const AddressList *address)
{
    auto stock = PoolAlloc<memcached_stock>(pool);

    stock->tcp_balancer = tcp_balancer;
    stock->address = address;

    return stock;
}

void
memcached_stock_free(gcc_unused struct memcached_stock *stock)
{
}

struct MemcachedStockRequest final : public StockGetHandler, Lease {
    struct pool *pool;

    StockItem *item;

    enum memcached_opcode opcode;

    const void *extras;
    size_t extras_length;

    const void *key;
    size_t key_length;

    Istream *value;

    const struct memcached_client_handler *handler;
    void *handler_ctx;

    struct async_operation_ref *async_ref;

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        item->Put(!reuse);
    }
};

/*
 * stock callback
 *
 */

void
MemcachedStockRequest::OnStockItemReady(StockItem &_item)
{
    item = &_item;

    memcached_client_invoke(pool, tcp_stock_item_get(_item),
                            tcp_stock_item_get_domain(_item) == AF_LOCAL
                            ? FdType::FD_SOCKET : FdType::FD_TCP,
                            *this,
                            opcode,
                            extras, extras_length,
                            key, key_length,
                            value,
                            handler, handler_ctx,
                            async_ref);
}

void
MemcachedStockRequest::OnStockItemError(GError *error)
{
    handler->error(error, handler_ctx);

    if (value != nullptr)
        value->CloseUnused();
}

void
memcached_stock_invoke(struct pool *pool, struct memcached_stock *stock,
                       enum memcached_opcode opcode,
                       const void *extras, size_t extras_length,
                       const void *key, size_t key_length,
                       Istream *value,
                       const struct memcached_client_handler *handler,
                       void *handler_ctx,
                       struct async_operation_ref *async_ref)
{
    auto request = PoolAlloc<MemcachedStockRequest>(*pool);

    assert(extras_length <= MEMCACHED_EXTRAS_MAX);
    assert(key_length <= MEMCACHED_KEY_MAX);

    request->pool = pool;
    request->opcode = opcode;
    request->extras = extras;
    request->extras_length = extras_length;
    request->key = key;
    request->key_length = key_length;
    request->value = value;
    request->handler = handler;
    request->handler_ctx = handler_ctx;
    request->async_ref = async_ref;

    tcp_balancer_get(*stock->tcp_balancer, *pool,
                     false, SocketAddress::Null(),
                     0, *stock->address,
                     10,
                     *request, *async_ref);
}
