/*
 * TCP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tcp-stock.h"
#include "stock.h"
#include "async.h"
#include "client-socket.h"
#include "uri-address.h"
#include "failure.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <event.h>

struct tcp_stock_connection {
    struct stock_item stock_item;
    const char *uri;

    struct async_operation create_operation;

    const struct sockaddr *addr;
    socklen_t addrlen;

    struct async_operation_ref client_socket;

    int fd;

    struct event event;
};


static const struct sockaddr *
uri_address_next_checked(struct uri_with_address *uwa, socklen_t *addrlen_r)
{
    const struct sockaddr *first = uri_address_next(uwa, addrlen_r), *ret = first;
    if (first == NULL)
        return NULL;

    do {
        if (!failure_check(first, *addrlen_r))
            return ret;

        ret = uri_address_next(uwa, addrlen_r);
        assert(ret != NULL);
    } while (ret != first);

    /* all addresses failed: */
    return first;
}


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
tcp_stock_event(int fd, short event __attr_unused, void *ctx)
{
    struct tcp_stock_connection *connection = ctx;
    char buffer;
    ssize_t nbytes;

    assert(fd == connection->fd);

    nbytes = read(fd, &buffer, sizeof(buffer));
    if (nbytes < 0)
        daemon_log(2, "error on idle TCP connection: %s\n",
                   strerror(errno));
    else if (nbytes > 0)
        daemon_log(2, "unexpected data in idle idle_socket\n");

    stock_del(&connection->stock_item);
    pool_commit();
}


/*
 * client_socket callback
 *
 */

static void
tcp_stock_socket_callback(int fd, int err, void *ctx)
{
    struct tcp_stock_connection *connection = ctx;

    async_ref_clear(&connection->client_socket);

    if (err == 0) {
        assert(fd >= 0);

        /* XXX check HTTP status code? */
        if (connection->addr != NULL)
            failure_remove(connection->addr, connection->addrlen);

        connection->fd = fd;
        connection->event.ev_events = 0;
        event_set(&connection->event, connection->fd, EV_READ,
                  tcp_stock_event, connection);

        stock_item_available(&connection->stock_item);
    } else {
        daemon_log(1, "failed to connect to '%s': %s\n",
                   connection->uri, strerror(err));

        if (connection->addr != NULL)
            failure_add(connection->addr, connection->addrlen);

        stock_item_failed(&connection->stock_item);
    }
}


/*
 * stock class
 *
 */

static pool_t
tcp_stock_pool(void *ctx __attr_unused, pool_t parent,
               const char *uri __attr_unused)
{
    return pool_new_linear(parent, "tcp_stock", 2048);
}

static void
tcp_stock_create(void *ctx __attr_unused, struct stock_item *item,
                 const char *uri, void *info,
                 struct async_operation_ref *async_ref)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;
    struct uri_with_address *uwa = info;

    assert(uri != NULL);

    async_ref_clear(&connection->client_socket);

    async_init(&connection->create_operation, &tcp_create_operation);
    async_ref_set(async_ref, &connection->create_operation);

    connection->uri = uri;

    if (uwa != NULL)
        connection->addr = uri_address_next_checked(uwa, &connection->addrlen);
    else
        connection->addr = NULL;

    if (connection->addr != NULL) {
        client_socket_new(connection->stock_item.pool,
                          connection->addr->sa_family, SOCK_STREAM, 0,
                          connection->addr, connection->addrlen,
                          tcp_stock_socket_callback, connection,
                          &connection->client_socket);
    } else if (uri[0] != '/') {
        daemon_log(1, "address missing for '%s'\n", uri);
        stock_item_failed(item);
    } else {
        /* HTTP over Unix socket */
        size_t path_length;
        struct sockaddr_un sun;

        path_length = strlen(uri);

        if (path_length >= sizeof(sun.sun_path)) {
            daemon_log(1, "client_socket_new() failed: unix socket path is too long\n");
            stock_item_failed(item);
            return;
        }

        sun.sun_family = AF_UNIX;
        memcpy(sun.sun_path, uri, path_length + 1);

        client_socket_new(connection->stock_item.pool,
                          PF_UNIX, SOCK_STREAM, 0,
                          (const struct sockaddr*)&sun, sizeof(sun),
                          tcp_stock_socket_callback, connection,
                          &connection->client_socket);
    }
}

static bool
tcp_stock_borrow(void *ctx __attr_unused, struct stock_item *item)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;

    event_del(&connection->event);
    connection->event.ev_events = 0;
    return true;
}

static void
tcp_stock_release(void *ctx __attr_unused, struct stock_item *item)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;

    event_add(&connection->event, NULL);
}

static void
tcp_stock_destroy(void *ctx __attr_unused, struct stock_item *item)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;

    if (async_ref_defined(&connection->client_socket))
        async_abort(&connection->client_socket);
    else if (connection->fd >= 0) {
        if (connection->event.ev_events != 0)
            event_del(&connection->event);
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
tcp_stock_new(pool_t pool)
{
    return hstock_new(pool, &tcp_stock_class, NULL);
}

int
tcp_stock_item_get(struct stock_item *item)
{
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;

    assert(item != NULL);

    return connection->fd;
}

