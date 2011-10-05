/*
 * TCP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcp-stock.h"
#include "stock.h"
#include "async.h"
#include "client-socket.h"
#include "address-list.h"
#include "address-envelope.h"
#include "pevent.h"

#include <daemon/log.h>
#include <socket/address.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

struct tcp_stock_request {
    const struct sockaddr *address;
    size_t address_length;
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
    return (struct tcp_stock_connection*)(((char*)ao) - offsetof(struct tcp_stock_connection, create_operation));
}

static void
tcp_create_abort(struct async_operation *ao)
{
    struct tcp_stock_connection *connection = async_to_tcp_stock_connection(ao);

    assert(connection != NULL);
    assert(async_ref_defined(&connection->client_socket));

    async_abort(&connection->client_socket);
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
    struct tcp_stock_connection *connection = ctx;

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
tcp_stock_socket_success(int fd, void *ctx)
{
    assert(fd >= 0);

    struct tcp_stock_connection *connection = ctx;
    async_ref_clear(&connection->client_socket);
    async_operation_finished(&connection->create_operation);

    connection->fd = fd;
    event_set(&connection->event, connection->fd, EV_READ|EV_TIMEOUT,
              tcp_stock_event, connection);

    stock_item_available(&connection->stock_item);
}

static void
tcp_stock_socket_timeout(void *ctx)
{
    struct tcp_stock_connection *connection = ctx;
    async_ref_clear(&connection->client_socket);
    async_operation_finished(&connection->create_operation);

    GError *error = g_error_new(g_file_error_quark(), ETIMEDOUT,
                                "failed to connect to '%s': timeout",
                                connection->uri);
    stock_item_failed(&connection->stock_item, error);
}

static void
tcp_stock_socket_error(GError *error, void *ctx)
{
    struct tcp_stock_connection *connection = ctx;
    async_ref_clear(&connection->client_socket);
    async_operation_finished(&connection->create_operation);

    g_prefix_error(&error, "failed to connect to '%s': ", connection->uri);
    stock_item_failed(&connection->stock_item, error);
}

static const struct client_socket_handler tcp_stock_socket_handler = {
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
    struct tcp_stock_request *request = info;

    assert(uri != NULL);

    async_ref_clear(&connection->client_socket);

    async_init(&connection->create_operation, &tcp_create_operation);
    async_ref_set(async_ref, &connection->create_operation);

    connection->uri = uri;

    connection->domain = request->address->sa_family;
    client_socket_new(caller_pool, connection->domain, SOCK_STREAM, 0,
                      request->address, request->address_length,
                      &tcp_stock_socket_handler, connection,
                      &connection->client_socket);
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
    struct timeval tv = {
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

    if (async_ref_defined(&connection->client_socket))
        async_abort(&connection->client_socket);
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
    return hstock_new(pool, &tcp_stock_class, NULL, limit);
}

void
tcp_stock_get(struct hstock *tcp_stock, struct pool *pool, const char *name,
              const struct sockaddr *address, size_t address_length,
              const struct stock_handler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref)
{
    assert(address != NULL);
    assert(address_length > 0);

    struct tcp_stock_request *request = p_malloc(pool, sizeof(*request));
    request->address = address;
    request->address_length = address_length;

    if (name == NULL) {
        char buffer[1024];
        if (!socket_address_to_string(buffer, sizeof(buffer),
                                      address, address_length))
            buffer[0] = 0;

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

    assert(item != NULL);

    return connection->fd;
}

int
tcp_stock_item_get_domain(const struct stock_item *item)
{
    const struct tcp_stock_connection *connection =
        (const struct tcp_stock_connection *)item;

    assert(item != NULL);
    assert(connection->fd >= 0);

    return connection->domain;
}
