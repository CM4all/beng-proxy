/*
 * TCP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcp_stock.hxx"
#include "hstock.hxx"
#include "stock.hxx"
#include "async.hxx"
#include "address_list.hxx"
#include "pevent.h"
#include "gerrno.h"
#include "pool.hxx"
#include "net/ConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/Cast.hxx"

#include <daemon/log.h>
#include <socket/address.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

struct tcp_stock_request {
    bool ip_transparent;

    SocketAddress bind_address, address;

    unsigned timeout;
};

struct tcp_stock_connection {
    struct stock_item stock_item;
    const char *uri;

    struct async_operation create_operation;

    struct async_operation_ref client_socket;

    int fd, domain;

    struct event event;
};

/*
 * async operation
 *
 */

static struct tcp_stock_connection *
async_to_tcp_stock_connection(struct async_operation *ao)
{
    return &ContainerCast2(*ao, &tcp_stock_connection::create_operation);
}

static void
tcp_create_abort(struct async_operation *ao)
{
    struct tcp_stock_connection *connection = async_to_tcp_stock_connection(ao);

    assert(connection != nullptr);
    assert(connection->client_socket.IsDefined());

    connection->client_socket.Abort();
    stock_item_aborted(&connection->stock_item);
}

static const struct async_operation_class tcp_create_operation = {
    .abort = tcp_create_abort,
};


/*
 * libevent callback
 *
 */

static void
tcp_stock_event(int fd, short event, void *ctx)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)ctx;

    assert(fd == connection->fd);

    p_event_consumed(&connection->event, connection->stock_item.pool);

    if ((event & EV_TIMEOUT) == 0) {
        char buffer;
        ssize_t nbytes;

        assert((event & EV_READ) != 0);

        nbytes = recv(fd, &buffer, sizeof(buffer), MSG_DONTWAIT);
        if (nbytes < 0)
            daemon_log(2, "error on idle TCP connection: %s\n",
                       strerror(errno));
        else if (nbytes > 0)
            daemon_log(2, "unexpected data in idle idle_socket\n");
    }

    stock_del(&connection->stock_item);
    pool_commit();
}


/*
 * client_socket callback
 *
 */

static void
tcp_stock_socket_success(SocketDescriptor &&fd, void *ctx)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)ctx;

    connection->client_socket.Clear();
    connection->create_operation.Finished();

    connection->fd = fd.Steal();
    event_set(&connection->event, connection->fd, EV_READ|EV_TIMEOUT,
              tcp_stock_event, connection);

    stock_item_available(&connection->stock_item);
}

static void
tcp_stock_socket_timeout(void *ctx)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)ctx;

    connection->client_socket.Clear();
    connection->create_operation.Finished();

    GError *error = g_error_new(errno_quark(), ETIMEDOUT,
                                "failed to connect to '%s': timeout",
                                connection->uri);
    stock_item_failed(&connection->stock_item, error);
}

static void
tcp_stock_socket_error(GError *error, void *ctx)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)ctx;

    connection->client_socket.Clear();
    connection->create_operation.Finished();

    g_prefix_error(&error, "failed to connect to '%s': ", connection->uri);
    stock_item_failed(&connection->stock_item, error);
}

static constexpr ConnectSocketHandler tcp_stock_socket_handler = {
    .success = tcp_stock_socket_success,
    .timeout = tcp_stock_socket_timeout,
    .error = tcp_stock_socket_error,
};


/*
 * stock class
 *
 */

static struct pool *
tcp_stock_pool(void *ctx gcc_unused, struct pool *parent,
               const char *uri gcc_unused)
{
    return pool_new_linear(parent, "tcp_stock", 2048);
}

static void
tcp_stock_create(void *ctx, struct stock_item *item,
                 const char *uri, void *info,
                 struct pool *caller_pool,
                 struct async_operation_ref *async_ref)
{
    (void)ctx;

    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;
    struct tcp_stock_request *request = (struct tcp_stock_request *)info;

    assert(uri != nullptr);

    connection->client_socket.Clear();

    connection->create_operation.Init(tcp_create_operation);
    async_ref->Set(connection->create_operation);

    connection->uri = uri;

    connection->domain = request->address.GetFamily();
    client_socket_new(*caller_pool, connection->domain, SOCK_STREAM, 0,
                      request->ip_transparent,
                      request->bind_address,
                      request->address,
                      request->timeout,
                      tcp_stock_socket_handler, connection,
                      connection->client_socket);
}

static bool
tcp_stock_borrow(void *ctx gcc_unused, struct stock_item *item)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;

    p_event_del(&connection->event, item->pool);
    return true;
}

static void
tcp_stock_release(void *ctx gcc_unused, struct stock_item *item)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;
    static const struct timeval tv = {
        .tv_sec = 60,
        .tv_usec = 0,
    };

    p_event_add(&connection->event, &tv, item->pool, "tcp_stock_event");
}

static void
tcp_stock_destroy(void *ctx gcc_unused, struct stock_item *item)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;

    if (connection->client_socket.IsDefined())
        connection->client_socket.Abort();
    else if (connection->fd >= 0) {
        p_event_del(&connection->event, item->pool);
        close(connection->fd);
    }
}

static const struct stock_class tcp_stock_class = {
    .item_size = sizeof(struct tcp_stock_connection),
    .pool = tcp_stock_pool,
    .create = tcp_stock_create,
    .borrow = tcp_stock_borrow,
    .release = tcp_stock_release,
    .destroy = tcp_stock_destroy,
};


/*
 * interface
 *
 */

struct hstock *
tcp_stock_new(struct pool *pool, unsigned limit)
{
    return hstock_new(pool, &tcp_stock_class, nullptr, limit, 16);
}

void
tcp_stock_get(struct hstock *tcp_stock, struct pool *pool, const char *name,
              bool ip_transparent,
              SocketAddress bind_address,
              SocketAddress address,
              unsigned timeout,
              const struct stock_get_handler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref)
{
    assert(!address.IsNull());

    auto request = NewFromPool<struct tcp_stock_request>(*pool);
    request->ip_transparent = ip_transparent;
    request->bind_address = bind_address;
    request->address = address;
    request->timeout = timeout;

    if (name == nullptr) {
        char buffer[1024];
        if (!socket_address_to_string(buffer, sizeof(buffer),
                                      address, address.GetSize()))
            buffer[0] = 0;

        if (!bind_address.IsNull()) {
            char bind_buffer[1024];
            if (!socket_address_to_string(bind_buffer, sizeof(bind_buffer),
                                          bind_address, bind_address.GetSize()))
                bind_buffer[0] = 0;
            name = p_strcat(pool, bind_buffer, ">", buffer, nullptr);
        } else
            name = p_strdup(pool, buffer);
    }

    hstock_get(tcp_stock, pool, name, request,
               handler, handler_ctx, async_ref);
}

void
tcp_stock_put(struct hstock *tcp_stock, struct stock_item *item, bool destroy)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;

    hstock_put(tcp_stock, connection->uri, item, destroy);
}

int
tcp_stock_item_get(const struct stock_item *item)
{
    const struct tcp_stock_connection *connection =
        (const struct tcp_stock_connection *)item;

    assert(item != nullptr);

    return connection->fd;
}

int
tcp_stock_item_get_domain(const struct stock_item *item)
{
    const struct tcp_stock_connection *connection =
        (const struct tcp_stock_connection *)item;

    assert(item != nullptr);
    assert(connection->fd >= 0);

    return connection->domain;
}
