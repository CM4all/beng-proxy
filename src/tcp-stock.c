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
#include "balancer.h"
#include "failure.h"
#include "pevent.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

struct tcp_stock_connection {
    struct stock_item stock_item;
    const char *uri;

    struct async_operation create_operation;

    const struct address_envelope *address;

    struct async_operation_ref client_socket;

    int fd, domain;

    struct event event;
};

static GQuark
tcp_stock_quark(void)
{
    return g_quark_from_static_string("tcp_stock");
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
tcp_stock_socket_callback(int fd, int err, void *ctx)
{
    struct tcp_stock_connection *connection = ctx;

    async_ref_clear(&connection->client_socket);
    async_operation_finished(&connection->create_operation);

    if (err == 0) {
        assert(fd >= 0);

        /* XXX check HTTP status code? */
        if (connection->address != NULL)
            failure_remove(&connection->address->address,
                           connection->address->length);

        connection->fd = fd;
        event_set(&connection->event, connection->fd, EV_READ|EV_TIMEOUT,
                  tcp_stock_event, connection);

        stock_item_available(&connection->stock_item);
    } else {
        GError *error = g_error_new(g_file_error_quark(), err,
                                    "failed to connect to '%s': %s",
                                    connection->uri, strerror(err));

        if (connection->address != NULL)
            failure_add(&connection->address->address,
                        connection->address->length);

        stock_item_failed(&connection->stock_item, error);
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
tcp_stock_create(void *ctx, struct stock_item *item,
                 const char *uri, void *info,
                 pool_t caller_pool,
                 struct async_operation_ref *async_ref)
{
    struct balancer *balancer = ctx;
    struct tcp_stock_connection *connection =
        (struct tcp_stock_connection *)item;
    const struct address_list *address_list = info;

    assert(uri != NULL);

    async_ref_clear(&connection->client_socket);

    async_init(&connection->create_operation, &tcp_create_operation);
    async_ref_set(async_ref, &connection->create_operation);

    connection->uri = uri;

    if (address_list != NULL)
        connection->address = balancer_get(balancer, address_list,
                                           0);
    else
        connection->address = NULL;

    if (connection->address != NULL) {
        connection->domain = connection->address->address.sa_family;
        client_socket_new(caller_pool,
                          connection->address->address.sa_family,
                          SOCK_STREAM, 0,
                          &connection->address->address,
                          connection->address->length,
                          tcp_stock_socket_callback, connection,
                          &connection->client_socket);
    } else if (uri[0] != '/') {
        GError *error = g_error_new(tcp_stock_quark(), 0,
                                    "address missing for '%s'", uri);

        async_operation_finished(&connection->create_operation);
        stock_item_failed(item, error);
    } else {
        /* HTTP over Unix socket */
        size_t path_length;
        struct sockaddr_un sun;

        path_length = strlen(uri);

        if (path_length >= sizeof(sun.sun_path)) {
            GError *error = g_error_new(tcp_stock_quark(), 0,
                                        "unix socket path is too long");

            async_operation_finished(&connection->create_operation);
            stock_item_failed(item, error);
            return;
        }

        sun.sun_family = AF_UNIX;
        memcpy(sun.sun_path, uri, path_length + 1);

        connection->domain = AF_LOCAL;
        client_socket_new(caller_pool,
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

    p_event_del(&connection->event, item->pool);
    return true;
}

static void
tcp_stock_release(void *ctx __attr_unused, struct stock_item *item)
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
tcp_stock_destroy(void *ctx __attr_unused, struct stock_item *item)
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
tcp_stock_new(pool_t pool, struct balancer *balancer, unsigned limit)
{
    return hstock_new(pool, &tcp_stock_class, balancer, limit);
}

void
tcp_stock_get(struct hstock *tcp_stock, pool_t pool, const char *name,
              const struct address_list *address_list,
              const struct stock_handler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref)
{
    union {
        const struct address_list *in;
        void *out;
    } u = {
        .in = address_list,
    };

    if (name == NULL) {
        assert(address_list != NULL);
        name = p_strdup(pool, address_list_key(address_list));
    }

    hstock_get(tcp_stock, pool, name, u.out,
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
