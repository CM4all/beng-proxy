/*
 * TCP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcp_stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "async.hxx"
#include "address_list.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "event/SocketEvent.hxx"
#include "event/Duration.hxx"
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

struct TcpStockConnection final : HeapStockItem, ConnectSocketHandler {
    struct async_operation create_operation;

    struct async_operation_ref client_socket;

    int fd = -1;

    const int domain;

    SocketEvent event;

    TcpStockConnection(CreateStockItem c, int _domain,
                       struct async_operation_ref &async_ref)
        :HeapStockItem(c), domain(_domain),
         event(c.stock.GetEventLoop(), BIND_THIS_METHOD(EventCallback)) {
        create_operation.Init2<TcpStockConnection,
                               &TcpStockConnection::create_operation>();
        async_ref.Set(create_operation);

        client_socket.Clear();
    }

    ~TcpStockConnection() override;

    void Abort() {
        assert(client_socket.IsDefined());

        client_socket.Abort();
        InvokeCreateAborted();
    }

    void EventCallback(short events);

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(SocketDescriptor &&fd) override;
    void OnSocketConnectError(GError *error) override;

    /* virtual methods from class StockItem */
    bool Borrow(gcc_unused void *ctx) override {
        event.Delete();
        return true;
    }

    bool Release(gcc_unused void *ctx) override {
        event.Add(EventDuration<60>::value);
        return true;
    }
};


/*
 * libevent callback
 *
 */

inline void
TcpStockConnection::EventCallback(short events)
{
    if ((events & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes;

        assert((events & EV_READ) != 0);

        nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle TCP connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data in idle TCP connection\n");
    }

    InvokeIdleDisconnect();
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
    event.Set(fd, EV_READ|EV_TIMEOUT);

    InvokeCreateSuccess();
}

void
TcpStockConnection::OnSocketConnectError(GError *error)
{
    client_socket.Clear();
    create_operation.Finished();

    g_prefix_error(&error, "failed to connect to '%s': ", GetStockName());
    InvokeCreateError(error);
}

/*
 * stock class
 *
 */

static void
tcp_stock_create(gcc_unused void *ctx,
                 CreateStockItem c,
                 void *info,
                 struct pool &caller_pool,
                 struct async_operation_ref &async_ref)
{
    TcpStockRequest *request = (TcpStockRequest *)info;

    auto *connection = new TcpStockConnection(c,
                                              request->address.GetFamily(),
                                              async_ref);

    client_socket_new(c.stock.GetEventLoop(), caller_pool,
                      connection->domain, SOCK_STREAM, 0,
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
    .create = tcp_stock_create,
};


/*
 * interface
 *
 */

StockMap *
tcp_stock_new(EventLoop &event_loop, unsigned limit)
{
    return new StockMap(event_loop, tcp_stock_class, nullptr,
                        limit, 16);
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

    tcp_stock->Get(*pool, name, request, handler, async_ref);
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
