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
#include "GException.hxx"
#include "istream/istream.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <sys/socket.h>

struct MemachedStock {
    EventLoop &event_loop;

    TcpBalancer &tcp_balancer;

    const AddressList &address;

    MemachedStock(EventLoop &_event_loop,
                  TcpBalancer &_tcp_balancer,
                  const AddressList &_address)
        :event_loop(_event_loop),
         tcp_balancer(_tcp_balancer),
         address(_address) {}
};

MemachedStock *
memcached_stock_new(EventLoop &event_loop, TcpBalancer &tcp_balancer,
                    const AddressList &address)
{
    return new MemachedStock(event_loop, tcp_balancer, address);
}

void
memcached_stock_free(MemachedStock *stock)
{
    delete stock;
}

struct MemcachedStockRequest final : public StockGetHandler, Lease {
    struct pool &pool;
    EventLoop &event_loop;

    StockItem *item;

    const enum memcached_opcode opcode;

    const void *const extras;
    const size_t extras_length;

    const void *const key;
    const size_t key_length;

    Istream *const value;

    const struct memcached_client_handler &handler;
    void *const handler_ctx;

    CancellablePointer &cancel_ptr;

    MemcachedStockRequest(struct pool &_pool, EventLoop &_event_loop,
                          enum memcached_opcode _opcode,
                          const void *_extras, size_t _extras_length,
                          const void *_key, size_t _key_length,
                          Istream *_value,
                          const struct memcached_client_handler &_handler,
                          void *_handler_ctx,
                          CancellablePointer &_cancel_ptr)
        :pool(_pool), event_loop(_event_loop),
         opcode(_opcode),
         extras(_extras), extras_length(_extras_length),
         key(_key), key_length(_key_length),
         value(_value),
         handler(_handler), handler_ctx(_handler_ctx),
         cancel_ptr(_cancel_ptr) {}

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(std::exception_ptr ep) override;

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

    memcached_client_invoke(&pool, event_loop,
                            tcp_stock_item_get(_item),
                            tcp_stock_item_get_domain(_item) == AF_LOCAL
                            ? FdType::FD_SOCKET : FdType::FD_TCP,
                            *this,
                            opcode,
                            extras, extras_length,
                            key, key_length,
                            value,
                            &handler, handler_ctx,
                            cancel_ptr);
}

void
MemcachedStockRequest::OnStockItemError(std::exception_ptr ep)
{
    handler.error(ToGError(ep), handler_ctx);

    if (value != nullptr)
        value->CloseUnused();
}

void
memcached_stock_invoke(struct pool &pool, MemachedStock &stock,
                       enum memcached_opcode opcode,
                       const void *extras, size_t extras_length,
                       const void *key, size_t key_length,
                       Istream *value,
                       const struct memcached_client_handler &handler,
                       void *handler_ctx,
                       CancellablePointer &cancel_ptr)
{
    assert(extras_length <= MEMCACHED_EXTRAS_MAX);
    assert(key_length <= MEMCACHED_KEY_MAX);

    auto request = NewFromPool<MemcachedStockRequest>(pool, pool,
                                                      stock.event_loop,
                                                      opcode,
                                                      extras, extras_length,
                                                      key, key_length,
                                                      value,
                                                      handler, handler_ctx,
                                                      cancel_ptr);

    tcp_balancer_get(stock.tcp_balancer, pool,
                     false, SocketAddress::Null(),
                     0, stock.address,
                     10,
                     *request, cancel_ptr);
}
