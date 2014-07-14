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
#include "util/Cast.hxx"

#include <inline/list.h>
#include <daemon/log.h>

struct nfs_stock_request {
    struct list_head siblings;

    struct nfs_stock_connection *connection;

    struct pool *pool;
    const struct nfs_stock_get_handler *handler;
    void *handler_ctx;

    struct async_operation operation;
};

struct nfs_stock_connection {
    struct list_head siblings;

    struct nfs_stock *stock;

    struct pool *pool;

    const char *key;

    struct nfs_client *client;

    struct async_operation_ref async_ref;

    struct list_head requests;

    nfs_stock_connection(struct nfs_stock &_stock, struct pool &_pool,
                         const char *_key)
        :stock(&_stock), pool(&_pool), key(_key), client(nullptr) {
        list_init(&requests);
    }
};

struct nfs_stock {
    struct pool *pool;

    /**
     * Maps server name to #nfs_stock_connection.
     */
    struct hashmap *connection_map;

    struct list_head connection_list;

    nfs_stock(struct pool &_pool)
        :pool(&_pool),
         connection_map(hashmap_new(pool, 59)) {
        list_init(&connection_list);
    }

    ~nfs_stock();
};

/*
 * nfs_client_handler
 *
 */

static void
nfs_stock_client_ready(struct nfs_client *client, void *ctx)
{
    struct nfs_stock_connection *const connection =
        (struct nfs_stock_connection *)ctx;
    assert(connection->client == nullptr);

    connection->client = client;

    while (!list_empty(&connection->requests)) {
        struct nfs_stock_request *request =
            (struct nfs_stock_request *)connection->requests.next;
        list_remove(&request->siblings);

        request->handler->ready(client, request->handler_ctx);
        DeleteUnrefTrashPool(*request->pool, request);
    }
}

static void
nfs_stock_client_mount_error(GError *error, void *ctx)
{
    struct nfs_stock_connection *const connection =
        (struct nfs_stock_connection *)ctx;
    struct nfs_stock *const stock = connection->stock;

    assert(!list_empty(&stock->connection_list));

    while (!list_empty(&connection->requests)) {
        struct nfs_stock_request *request =
            (struct nfs_stock_request *)connection->requests.next;
        list_remove(&request->siblings);

        request->handler->error(g_error_copy(error), request->handler_ctx);
        DeleteUnrefTrashPool(*request->pool, request);
    }

    g_error_free(error);

    list_remove(&connection->siblings);
    hashmap_remove_existing(stock->connection_map, connection->key,
                            connection);
    DeleteUnrefTrashPool(*connection->pool, connection);
}

static void
nfs_stock_client_closed(GError *error, void *ctx)
{
    struct nfs_stock_connection *const connection =
        (struct nfs_stock_connection *)ctx;
    struct nfs_stock *const stock = connection->stock;

    assert(list_empty(&connection->requests));
    assert(!list_empty(&stock->connection_list));

    daemon_log(1, "Connection to %s closed: %s\n",
               connection->key, error->message);
    g_error_free(error);

    list_remove(&connection->siblings);
    hashmap_remove_existing(stock->connection_map, connection->key,
                            connection);
    DeleteUnrefTrashPool(*connection->pool, connection);
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

static struct nfs_stock_request *
operation_to_nfs_stock_request(struct async_operation *ao)
{
    return ContainerCast(ao, struct nfs_stock_request, operation);
}

static void
nfs_stock_request_operation_abort(struct async_operation *ao)
{
    struct nfs_stock_request *const request = operation_to_nfs_stock_request(ao);

    list_remove(&request->siblings);
    DeleteUnrefTrashPool(*request->pool, request);

    // TODO: abort client if all requests are gone?
}

static const struct async_operation_class nfs_stock_request_operation = {
    .abort = nfs_stock_request_operation_abort,
};

/*
 * public
 *
 */

struct nfs_stock *
nfs_stock_new(struct pool *pool)
{
    return NewFromPool<struct nfs_stock>(*pool, *pool);
}

nfs_stock::~nfs_stock()
{
    if (!list_empty(&connection_list)) {
        struct nfs_stock_connection *connection =
            (struct nfs_stock_connection *)connection_list.next;
        list_remove(&connection->siblings);
        hashmap_remove_existing(connection_map, connection->key,
                                connection);

        if (connection->client != nullptr)
            nfs_client_free(connection->client);
        else
            async_abort(&connection->async_ref);

        assert(list_empty(&connection->requests));
        DeleteUnrefTrashPool(*connection->pool, connection);
    }
}

void
nfs_stock_free(struct nfs_stock *stock)
{
    DeleteFromPool(*stock->pool, stock);
}

void
nfs_stock_get(struct nfs_stock *stock, struct pool *pool,
              const char *server, const char *export_name,
              const struct nfs_stock_get_handler *handler, void *ctx,
              struct async_operation_ref *async_ref)
{
    const char *key = p_strcat(pool, server, ":", export_name, nullptr);

    struct nfs_stock_connection *connection = (struct nfs_stock_connection *)
        hashmap_get(stock->connection_map, key);
    const bool is_new = connection == nullptr;
    if (is_new) {
        struct pool *c_pool = pool_new_libc(stock->pool,
                                            "nfs_stock_connection");
        connection =
            NewFromPool<struct nfs_stock_connection>(*c_pool, *stock, *c_pool,
                                                     p_strdup(c_pool, key));

        hashmap_add(stock->connection_map, connection->key, connection);
        list_add(&connection->siblings, &stock->connection_list);
    } else if (connection->client != nullptr) {
        /* already connected */
        handler->ready(connection->client, ctx);
        return;
    }

    pool_ref(pool);
    auto request = NewFromPool<struct nfs_stock_request>(*pool);
    request->connection = connection;
    request->pool = pool;
    request->handler = handler;
    request->handler_ctx = ctx;
    async_init(&request->operation, &nfs_stock_request_operation);
    async_ref_set(async_ref, &request->operation);

    list_add(&request->siblings, &connection->requests);

    if (is_new)
        nfs_client_new(connection->pool, server, export_name,
                       &nfs_stock_client_handler, connection,
                       &connection->async_ref);
}
