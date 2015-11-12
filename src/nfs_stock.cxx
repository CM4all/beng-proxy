/*
 * NFS connection manager.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_stock.hxx"
#include "nfs_client.hxx"
#include "hashmap.hxx"
#include "pool.hxx"
#include "async.hxx"

#include <inline/list.h>
#include <daemon/log.h>

struct NfsStockConnection;

struct NfsStockRequest {
    struct list_head siblings;

    NfsStockConnection &connection;

    struct pool &pool;
    const NfsStockGetHandler &handler;
    void *handler_ctx;

    struct async_operation operation;

    NfsStockRequest(NfsStockConnection &_connection, struct pool &_pool,
                    const NfsStockGetHandler &_handler, void *ctx,
                    struct async_operation_ref &async_ref)
        :connection(_connection), pool(_pool),
         handler(_handler), handler_ctx(ctx) {
        pool_ref(&pool);
        operation.Init2<NfsStockRequest>();
        async_ref.Set(operation);
    }

    void Abort();
};

struct NfsStockConnection {
    struct list_head siblings;

    NfsStock &stock;

    struct pool &pool;

    const char *key;

    struct nfs_client *client;

    struct async_operation_ref async_ref;

    struct list_head requests;

    NfsStockConnection(NfsStock &_stock, struct pool &_pool,
                       const char *_key)
        :stock(_stock), pool(_pool), key(_key), client(nullptr) {
        list_init(&requests);
    }
};

struct NfsStock {
    struct pool &pool;

    /**
     * Maps server name to #NfsStockConnection.
     */
    struct hashmap *connection_map;

    struct list_head connection_list;

    NfsStock(struct pool &_pool)
        :pool(_pool),
         connection_map(hashmap_new(&pool, 59)) {
        list_init(&connection_list);
    }

    ~NfsStock();

    void Get(struct pool &pool,
             const char *server, const char *export_name,
             const NfsStockGetHandler &handler, void *ctx,
             struct async_operation_ref &async_ref);
};

/*
 * nfs_client_handler
 *
 */

static void
nfs_stock_client_ready(struct nfs_client *client, void *ctx)
{
    auto *const connection = (NfsStockConnection *)ctx;
    assert(connection->client == nullptr);

    connection->client = client;

    while (!list_empty(&connection->requests)) {
        auto *request = (NfsStockRequest *)connection->requests.next;
        list_remove(&request->siblings);

        request->handler.ready(client, request->handler_ctx);
        DeleteUnrefPool(request->pool, request);
    }
}

static void
nfs_stock_client_mount_error(GError *error, void *ctx)
{
    auto *const connection = (NfsStockConnection *)ctx;
    NfsStock &stock = connection->stock;

    assert(!list_empty(&stock.connection_list));

    while (!list_empty(&connection->requests)) {
        auto *request = (NfsStockRequest *)connection->requests.next;
        list_remove(&request->siblings);

        request->handler.error(g_error_copy(error), request->handler_ctx);
        DeleteUnrefPool(request->pool, request);
    }

    g_error_free(error);

    list_remove(&connection->siblings);
    hashmap_remove_existing(stock.connection_map, connection->key,
                            connection);
    DeleteUnrefTrashPool(connection->pool, connection);
}

static void
nfs_stock_client_closed(GError *error, void *ctx)
{
    auto *const connection = (NfsStockConnection *)ctx;
    NfsStock &stock = connection->stock;

    assert(list_empty(&connection->requests));
    assert(!list_empty(&stock.connection_list));

    daemon_log(1, "Connection to %s closed: %s\n",
               connection->key, error->message);
    g_error_free(error);

    list_remove(&connection->siblings);
    hashmap_remove_existing(stock.connection_map, connection->key,
                            connection);
    DeleteUnrefTrashPool(connection->pool, connection);
}

static const struct nfs_client_handler nfs_stock_client_handler = {
    .ready = nfs_stock_client_ready,
    .mount_error = nfs_stock_client_mount_error,
    .closed = nfs_stock_client_closed,
};

/*
 * async operation
 *
 */

inline void
NfsStockRequest::Abort()
{
    list_remove(&siblings);
    DeleteUnrefPool(pool, this);

    // TODO: abort client if all requests are gone?
}

/*
 * public
 *
 */

NfsStock *
nfs_stock_new(struct pool *pool)
{
    return new NfsStock(*pool);
}

NfsStock::~NfsStock()
{
    if (!list_empty(&connection_list)) {
        auto *connection = (NfsStockConnection *)connection_list.next;
        list_remove(&connection->siblings);
        hashmap_remove_existing(connection_map, connection->key,
                                connection);

        if (connection->client != nullptr)
            nfs_client_free(connection->client);
        else
            connection->async_ref.Abort();

        assert(list_empty(&connection->requests));
        DeleteUnrefTrashPool(connection->pool, connection);
    }
}

void
nfs_stock_free(NfsStock *stock)
{
    delete stock;
}

inline void
NfsStock::Get(struct pool &caller_pool,
              const char *server, const char *export_name,
              const NfsStockGetHandler &handler, void *ctx,
              struct async_operation_ref &async_ref)
{
    const char *key = p_strcat(&caller_pool, server, ":", export_name,
                               nullptr);

    auto *connection = (NfsStockConnection *)
        hashmap_get(connection_map, key);
    const bool is_new = connection == nullptr;
    if (is_new) {
        struct pool *c_pool = pool_new_libc(&pool, "nfs_stock_connection");
        connection =
            NewFromPool<NfsStockConnection>(*c_pool, *this, *c_pool,
                                            p_strdup(c_pool, key));

        hashmap_add(connection_map, connection->key, connection);
        list_add(&connection->siblings, &connection_list);
    } else if (connection->client != nullptr) {
        /* already connected */
        handler.ready(connection->client, ctx);
        return;
    }

    auto request = NewFromPool<NfsStockRequest>(caller_pool, *connection,
                                                caller_pool, handler, ctx,
                                                async_ref);
    list_add(&request->siblings, &connection->requests);

    if (is_new)
        nfs_client_new(&connection->pool, server, export_name,
                       &nfs_stock_client_handler, connection,
                       &connection->async_ref);
}

void
nfs_stock_get(NfsStock *stock, struct pool *pool,
              const char *server, const char *export_name,
              const NfsStockGetHandler *handler, void *ctx,
              struct async_operation_ref *async_ref)
{
    stock->Get(*pool, server, export_name, *handler, ctx, *async_ref);
}
