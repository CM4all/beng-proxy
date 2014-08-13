/*
 * Stock of connections to a memcached server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "memcached_stock.hxx"
#include "memcached_client.hxx"
#include "stock.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "address_list.hxx"
#include "lease.hxx"
#include "pool.hxx"
#include "istream.h"
#include "net/SocketAddress.hxx"

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <sys/socket.h>

struct memcached_stock {
    struct tcp_balancer *tcp_balancer;

    const AddressList *address;
};

struct memcached_stock *
memcached_stock_new(struct pool &pool, struct tcp_balancer *tcp_balancer,
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

struct memcached_stock_request {
    struct pool *pool;

    struct memcached_stock *stock;
    struct stock_item *item;

    enum memcached_opcode opcode;

    const void *extras;
    size_t extras_length;

    const void *key;
    size_t key_length;

    struct istream *value;

    const struct memcached_client_handler *handler;
    void *handler_ctx;

    struct async_operation_ref *async_ref;
};

/*
 * socket lease
 *
 */

static void
memcached_socket_release(bool reuse, void *ctx)
{
    memcached_stock_request *request = (memcached_stock_request *)ctx;

    tcp_balancer_put(request->stock->tcp_balancer, request->item, !reuse);
}

static const struct lease memcached_socket_lease = {
    .release = memcached_socket_release,
};


/*
 * stock callback
 *
 */

static void
memcached_stock_ready(struct stock_item *item, void *ctx)
{
    memcached_stock_request *request = (memcached_stock_request *)ctx;

    request->item = item;

    memcached_client_invoke(request->pool, tcp_stock_item_get(item),
                            tcp_stock_item_get_domain(item) == AF_LOCAL
                            ? ISTREAM_SOCKET : ISTREAM_TCP,
                            &memcached_socket_lease, request,
                            request->opcode,
                            request->extras, request->extras_length,
                            request->key, request->key_length,
                            request->value,
                            request->handler, request->handler_ctx,
                            request->async_ref);
}

static void
memcached_stock_error(GError *error, void *ctx)
{
    memcached_stock_request *request = (memcached_stock_request *)ctx;

    request->handler->error(error, request->handler_ctx);

    if (request->value != nullptr)
        istream_close_unused(request->value);
}

static const struct stock_get_handler memcached_stock_handler = {
    .ready = memcached_stock_ready,
    .error = memcached_stock_error,
};

void
memcached_stock_invoke(struct pool *pool, struct memcached_stock *stock,
                       enum memcached_opcode opcode,
                       const void *extras, size_t extras_length,
                       const void *key, size_t key_length,
                       struct istream *value,
                       const struct memcached_client_handler *handler,
                       void *handler_ctx,
                       struct async_operation_ref *async_ref)
{
    auto request = PoolAlloc<memcached_stock_request>(*pool);

    assert(extras_length <= MEMCACHED_EXTRAS_MAX);
    assert(key_length <= MEMCACHED_KEY_MAX);

    request->pool = pool;
    request->stock = stock;
    request->opcode = opcode;
    request->extras = extras;
    request->extras_length = extras_length;
    request->key = key;
    request->key_length = key_length;
    request->value = value;
    request->handler = handler;
    request->handler_ctx = handler_ctx;
    request->async_ref = async_ref;

    tcp_balancer_get(stock->tcp_balancer, pool,
                     false, SocketAddress::Null(),
                     0, stock->address,
                     10,
                     &memcached_stock_handler, request,
                     async_ref);
}
