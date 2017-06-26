/*
 * NFS connection manager.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Stock.hxx"
#include "Client.hxx"
#include "Handler.hxx"
#include "pool.hxx"
#include "util/Cancellable.hxx"

#include <daemon/log.h>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>

#include <glib.h>

#include <string.h>

struct NfsStockConnection;

struct NfsStockRequest final
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
      Cancellable {
    NfsStockConnection &connection;

    struct pool &pool;
    NfsStockGetHandler &handler;

    NfsStockRequest(NfsStockConnection &_connection, struct pool &_pool,
                    NfsStockGetHandler &_handler,
                    CancellablePointer &cancel_ptr)
        :connection(_connection), pool(_pool),
         handler(_handler) {
        pool_ref(&pool);
        cancel_ptr = *this;
    }

    /* virtual methods from class Cancellable */
    void Cancel() override;
};

struct NfsStockConnection
    : NfsClientHandler,
      boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    NfsStock &stock;

    struct pool &pool;

    const char *key;

    NfsClient *client;

    CancellablePointer cancel_ptr;

    boost::intrusive::list<NfsStockRequest,
                           boost::intrusive::constant_time_size<false>> requests;

    NfsStockConnection(NfsStock &_stock, struct pool &_pool,
                       const char *_key)
        :stock(_stock), pool(_pool), key(_key), client(nullptr) {}

    void Remove(NfsStockRequest &r) {
        requests.erase(requests.iterator_to(r));
    }

    /* virtual methods from NfsClientHandler */
    void OnNfsClientReady(NfsClient &client) override;
    void OnNfsMountError(GError *error) override;
    void OnNfsClientClosed(GError *error) override;

    struct Compare {
        bool operator()(const NfsStockConnection &a, const NfsStockConnection &b) const {
            return strcmp(a.key, b.key) < 0;
        }

        bool operator()(const NfsStockConnection &a, const char *b) const {
            return strcmp(a.key, b) < 0;
        }

        bool operator()(const char *a, const NfsStockConnection &b) const {
            return strcmp(a, b.key) < 0;
        }
    };
};

struct NfsStock {
    EventLoop &event_loop;
    struct pool &pool;

    /**
     * Maps server name to #NfsStockConnection.
     */
    typedef boost::intrusive::set<NfsStockConnection,
                                  boost::intrusive::compare<NfsStockConnection::Compare>,
                                  boost::intrusive::constant_time_size<false>> ConnectionMap;
    ConnectionMap connections;

    NfsStock(EventLoop &_event_loop, struct pool &_pool)
        :event_loop(_event_loop), pool(_pool) {}

    ~NfsStock();

    void Get(struct pool &pool,
             const char *server, const char *export_name,
             NfsStockGetHandler &handler,
             CancellablePointer &cancel_ptr);

    void Remove(NfsStockConnection &c) {
        connections.erase(connections.iterator_to(c));
    }
};

/*
 * NfsClientHandler
 *
 */

void
NfsStockConnection::OnNfsClientReady(NfsClient &_client)
{
    assert(client == nullptr);

    client = &_client;

    requests.clear_and_dispose([&_client](NfsStockRequest *request){
            request->handler.OnNfsStockReady(_client);
            DeleteUnrefPool(request->pool, request);
        });
}

void
NfsStockConnection::OnNfsMountError(GError *error)
{
    assert(!stock.connections.empty());

    requests.clear_and_dispose([error](NfsStockRequest *request){
            request->handler.OnNfsStockError(g_error_copy(error));
            DeleteUnrefPool(request->pool, request);
        });

    g_error_free(error);

    stock.Remove(*this);
    DeleteUnrefTrashPool(pool, this);
}

void
NfsStockConnection::OnNfsClientClosed(GError *error)
{
    assert(requests.empty());
    assert(!stock.connections.empty());

    daemon_log(1, "Connection to %s closed: %s\n", key, error->message);
    g_error_free(error);

    stock.Remove(*this);
    DeleteUnrefTrashPool(pool, this);
}

/*
 * async operation
 *
 */

void
NfsStockRequest::Cancel()
{
    connection.Remove(*this);
    DeleteUnrefPool(pool, this);

    // TODO: abort client if all requests are gone?
}

/*
 * public
 *
 */

NfsStock *
nfs_stock_new(EventLoop &event_loop, struct pool &pool)
{
    return new NfsStock(event_loop, pool);
}

NfsStock::~NfsStock()
{
    connections.clear_and_dispose([this](NfsStockConnection *connection){
        if (connection->client != nullptr)
            nfs_client_free(connection->client);
        else
            connection->cancel_ptr.Cancel();

        assert(connection->requests.empty());
        DeleteUnrefTrashPool(connection->pool, connection);
        });
}

void
nfs_stock_free(NfsStock *stock)
{
    delete stock;
}

inline void
NfsStock::Get(struct pool &caller_pool,
              const char *server, const char *export_name,
              NfsStockGetHandler &handler,
              CancellablePointer &cancel_ptr)
{
    const char *key = p_strcat(&caller_pool, server, ":", export_name,
                               nullptr);

    ConnectionMap::insert_commit_data hint;
    auto result = connections.insert_check(key,
                                           NfsStockConnection::Compare(),
                                           hint);
    const bool is_new = result.second;
    NfsStockConnection *connection;
    if (is_new) {
        struct pool *c_pool = pool_new_libc(&pool, "nfs_stock_connection");
        connection =
            NewFromPool<NfsStockConnection>(*c_pool, *this, *c_pool,
                                            p_strdup(c_pool, key));

        connections.insert_commit(*connection, hint);
    } else {
        connection = &*result.first;
        if (connection->client != nullptr) {
            /* already connected */
            handler.OnNfsStockReady(*connection->client);
            return;
        }
    }

    auto request = NewFromPool<NfsStockRequest>(caller_pool, *connection,
                                                caller_pool, handler,
                                                cancel_ptr);
    connection->requests.push_front(*request);

    if (is_new)
        nfs_client_new(connection->stock.event_loop, connection->pool,
                       server, export_name,
                       *connection, connection->cancel_ptr);
}

void
nfs_stock_get(NfsStock *stock, struct pool *pool,
              const char *server, const char *export_name,
              NfsStockGetHandler &handler,
              CancellablePointer &cancel_ptr)
{
    stock->Get(*pool, server, export_name, handler, cancel_ptr);
}
