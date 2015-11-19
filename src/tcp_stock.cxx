/*
 * TCP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcp_stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "async.hxx"
#include "address_list.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "event/Event.hxx"
#include "event/Callback.hxx"
#include "net/ConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketDescriptor.hxx"

#include <daemon/log.h>
#include <socket/address.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

struct TcpStockRequest {
    bool ip_transparent;

    SocketAddress bind_address, address;

    unsigned timeout;
};

struct TcpStockConnection final : PoolStockItem, ConnectSocketHandler {
    const char *uri;

    struct async_operation create_operation;

    struct async_operation_ref client_socket;

    int fd = -1, domain;

    Event event;

    explicit TcpStockConnection(CreateStockItem c)
        :PoolStockItem(c) {
        client_socket.Clear();
    }

    ~TcpStockConnection() override;

    void Abort() {
        assert(client_socket.IsDefined());

        client_socket.Abort();
        stock_item_aborted(*this);
    }

    void EventCallback(int fd, short events);

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(SocketDescriptor &&fd) override;
    void OnSocketConnectError(GError *error) override;

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        event.Delete();
        return true;
    }

    void Release(gcc_unused void *ctx) override {
        static constexpr struct timeval tv = {
            .tv_sec = 60,
            .tv_usec = 0,
        };

        event.Add(tv);
    }
};


/*
 * libevent callback
 *
 */

inline void
TcpStockConnection::EventCallback(int _fd, short events)
{
    assert(_fd == fd);

    if ((events & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes;

        assert((events & EV_READ) != 0);

        nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle TCP connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data in idle idle_socket\n");
    }

    stock_del(*this);
    pool_commit();
}


/*
 * client_socket callback
 *
 */

void
TcpStockConnection::OnSocketConnectSuccess(SocketDescriptor &&new_fd)
{
    client_socket.Clear();
    create_operation.Finished();

    fd = new_fd.Steal();
    event.Set(fd, EV_READ|EV_TIMEOUT,
              MakeEventCallback(TcpStockConnection, EventCallback), this);

    stock_item_available(*this);
}

void
TcpStockConnection::OnSocketConnectError(GError *error)
{
    client_socket.Clear();
    create_operation.Finished();

    g_prefix_error(&error, "failed to connect to '%s': ", uri);
    stock_item_failed(*this, error);
}

/*
 * stock class
 *
 */

static struct pool *
tcp_stock_pool(gcc_unused void *ctx, struct pool &parent,
               gcc_unused const char *uri)
{
    return pool_new_linear(&parent, "tcp_stock", 2048);
}

static void
tcp_stock_create(gcc_unused void *ctx, CreateStockItem c,
                 const char *uri, void *info,
                 struct pool &caller_pool,
                 struct async_operation_ref &async_ref)
{
    assert(uri != nullptr);

    TcpStockRequest *request = (TcpStockRequest *)info;

    auto *connection = NewFromPool<TcpStockConnection>(c.pool, c);

    connection->create_operation.Init2<TcpStockConnection,
                                       &TcpStockConnection::create_operation>();
    async_ref.Set(connection->create_operation);

    connection->uri = uri;

    connection->domain = request->address.GetFamily();
    client_socket_new(caller_pool, connection->domain, SOCK_STREAM, 0,
                      request->ip_transparent,
                      request->bind_address,
                      request->address,
                      request->timeout,
                      *connection,
                      connection->client_socket);
}

TcpStockConnection::~TcpStockConnection()
{
    if (client_socket.IsDefined())
        client_socket.Abort();
    else if (fd >= 0) {
        event.Delete();
        close(fd);
    }
}

static constexpr StockClass tcp_stock_class = {
    .pool = tcp_stock_pool,
    .create = tcp_stock_create,
};


/*
 * interface
 *
 */

StockMap *
tcp_stock_new(struct pool *pool, unsigned limit)
{
    return hstock_new(*pool, tcp_stock_class, nullptr, limit, 16);
}

void
tcp_stock_get(StockMap *tcp_stock, struct pool *pool, const char *name,
              bool ip_transparent,
              SocketAddress bind_address,
              SocketAddress address,
              unsigned timeout,
              StockGetHandler &handler,
              struct async_operation_ref &async_ref)
{
    assert(!address.IsNull());

    auto request = NewFromPool<TcpStockRequest>(*pool);
    request->ip_transparent = ip_transparent;
    request->bind_address = bind_address;
    request->address = address;
    request->timeout = timeout;

    if (name == nullptr) {
        char buffer[1024];
        if (!socket_address_to_string(buffer, sizeof(buffer),
                                      address.GetAddress(), address.GetSize()))
            buffer[0] = 0;

        if (!bind_address.IsNull()) {
            char bind_buffer[1024];
            if (!socket_address_to_string(bind_buffer, sizeof(bind_buffer),
                                          bind_address.GetAddress(),
                                          bind_address.GetSize()))
                bind_buffer[0] = 0;
            name = p_strcat(pool, bind_buffer, ">", buffer, nullptr);
        } else
            name = p_strdup(pool, buffer);
    }

    hstock_get(*tcp_stock, *pool, name, request,
               handler, async_ref);
}

void
tcp_stock_put(StockMap *tcp_stock, StockItem &item, bool destroy)
{
    auto *connection = (TcpStockConnection *)&item;

    hstock_put(*tcp_stock, connection->uri, item, destroy);
}

int
tcp_stock_item_get(const StockItem &item)
{
    auto *connection = (const TcpStockConnection *)&item;

    return connection->fd;
}

int
tcp_stock_item_get_domain(const StockItem &item)
{
    auto *connection = (const TcpStockConnection *)&item;

    assert(connection->fd >= 0);

    return connection->domain;
}

const char *
tcp_stock_item_get_name(const StockItem &item)
{
    auto *connection = (const TcpStockConnection *)&item;

    return connection->uri;
}
