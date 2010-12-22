/*
 * Stock of connections to a memcached server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "memcached-stock.h"
#include "memcached-client.h"
#include "stock.h"
#include "tcp-stock.h"
#include "uri-address.h"
#include "lease.h"

#include <glib.h>
#include <assert.h>
#include <string.h>
#include <sys/socket.h>

struct memcached_stock {
    struct hstock *tcp_stock;

    struct uri_with_address *address;
};

struct memcached_stock *
memcached_stock_new(pool_t pool, struct hstock *tcp_stock,
                    struct uri_with_address *address)
{
    struct memcached_stock *stock = p_malloc(pool, sizeof(*stock));

    stock->tcp_stock = tcp_stock;
    stock->address = address;

    return stock;
}

void
memcached_stock_free(G_GNUC_UNUSED struct memcached_stock *stock)
{
}

struct memcached_stock_request {
    pool_t pool;

    struct memcached_stock *stock;
    struct stock_item *item;

    enum memcached_opcode opcode;

    const void *extras;
    size_t extras_length;

    const void *key;
    size_t key_length;

    istream_t value;

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
    struct memcached_stock_request *request = ctx;

    hstock_put(request->stock->tcp_stock, request->stock->address->uri,
               request->item, !reuse);
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
    struct memcached_stock_request *request = ctx;

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
memcached_stock_error(void *ctx)
{
    struct memcached_stock_request *request = ctx;

    GError *error = g_error_new_literal(memcached_client_quark(), 0,
                                        "memcached stock request failed");
    request->handler->error(error, request->handler_ctx);

    if (request->value != NULL)
        istream_close_unused(request->value);
}

static const struct stock_handler memcached_stock_handler = {
    .ready = memcached_stock_ready,
    .error = memcached_stock_error,
};

void
memcached_stock_invoke(pool_t pool, struct memcached_stock *stock,
                       enum memcached_opcode opcode,
                       const void *extras, size_t extras_length,
                       const void *key, size_t key_length,
                       istream_t value,
                       const struct memcached_client_handler *handler,
                       void *handler_ctx,
                       struct async_operation_ref *async_ref)
{
    struct memcached_stock_request *request = p_malloc(pool, sizeof(*request));

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

    hstock_get(stock->tcp_stock, pool,
               stock->address->uri, stock->address,
               &memcached_stock_handler, request,
               async_ref);
}
