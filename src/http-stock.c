/*
 * HTTP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-stock.h"
#include "stock.h"
#include "async.h"
#include "client-socket.h"
#include "http-client.h"
#include "uri-address.h"
#include "failure.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/un.h>

struct http_stock_connection {
    struct stock_item stock_item;
    const char *uri;

    struct async_operation create_operation;

    const struct sockaddr *addr;
    socklen_t addrlen;

    struct async_operation_ref client_socket;
    struct http_client_connection *http;

    bool destroyed;
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

static struct http_stock_connection *
async_to_http_stock_connection(struct async_operation *ao)
{
    return (struct http_stock_connection*)(((char*)ao) - offsetof(struct http_stock_connection, create_operation));
}

static void
url_create_abort(struct async_operation *ao)
{
    struct http_stock_connection *connection = async_to_http_stock_connection(ao);

    assert(connection != NULL);
    assert(async_ref_defined(&connection->client_socket));

    async_abort(&connection->client_socket);
    stock_item_aborted(&connection->stock_item);
}

static struct async_operation_class url_create_operation = {
    .abort = url_create_abort,
};


/*
 * http_client connection handler
 *
 */

static void
http_stock_connection_idle(void *ctx)
{
    struct http_stock_connection *connection = ctx;

    stock_put(&connection->stock_item, false);
}

static void
http_stock_connection_free(void *ctx)
{
    struct http_stock_connection *connection = ctx;

    assert(connection->http != NULL);

    if (connection->destroyed)
        return;

    if (stock_item_is_idle(&connection->stock_item))
        stock_del(&connection->stock_item);
    else
        stock_put(&connection->stock_item, true);
}

static const struct http_client_connection_handler http_stock_connection_handler = {
    .idle = http_stock_connection_idle,
    .free = http_stock_connection_free,
};


/*
 * client_socket callback
 *
 */

static void
http_stock_socket_callback(int fd, int err, void *ctx)
{
    struct http_stock_connection *connection = ctx;

    async_ref_clear(&connection->client_socket);

    if (err == 0) {
        assert(fd >= 0);

        /* XXX check HTTP status code? */
        failure_remove(connection->addr, connection->addrlen);

        connection->http = http_client_connection_new(connection->stock_item.pool, fd,
                                                      &http_stock_connection_handler, connection);
        stock_item_available(&connection->stock_item);
    } else {
        daemon_log(1, "failed to connect to '%s': %s\n",
                   connection->uri, strerror(err));

        failure_add(connection->addr, connection->addrlen);
        stock_item_failed(&connection->stock_item);
    }
}


/*
 * stock class
 *
 */

static pool_t
http_stock_pool(void *ctx __attr_unused, pool_t parent,
               const char *uri __attr_unused)
{
    return pool_new_linear(parent, "http_stock", 2048);
}

static void
http_stock_create(void *ctx __attr_unused, struct stock_item *item,
                  const char *uri, void *info,
                  struct async_operation_ref *async_ref)
{
    struct http_stock_connection *connection =
        (struct http_stock_connection *)item;
    struct uri_with_address *uwa = info;

    assert(uri != NULL);

    async_ref_clear(&connection->client_socket);
    connection->http = NULL;
    connection->destroyed = false;

    async_init(&connection->create_operation,
               &url_create_operation);
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
                          http_stock_socket_callback, connection,
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
                          http_stock_socket_callback, connection,
                          &connection->client_socket);
    }
}

static bool
http_stock_validate(void *ctx __attr_unused, struct stock_item *item)
{
    struct http_stock_connection *connection =
        (struct http_stock_connection *)item;

    return connection->http != NULL;
}

static void
http_stock_destroy(void *ctx __attr_unused, struct stock_item *item)
{
    struct http_stock_connection *connection =
        (struct http_stock_connection *)item;

    connection->destroyed = true;

    if (async_ref_defined(&connection->client_socket))
        async_abort(&connection->client_socket);
    else if (connection->http != NULL)
        http_client_connection_close(connection->http);
}

static struct stock_class http_stock_class = {
    .item_size = sizeof(struct http_stock_connection),
    .pool = http_stock_pool,
    .create = http_stock_create,
    .validate = http_stock_validate,
    .destroy = http_stock_destroy,
};


/*
 * interface
 *
 */

struct hstock *
http_stock_new(pool_t pool)
{
    return hstock_new(pool, &http_stock_class, NULL);
}

struct http_client_connection *
http_stock_item_get(struct stock_item *item)
{
    struct http_stock_connection *connection =
        (struct http_stock_connection *)item;

    assert(item != NULL);

    return connection->http;
}
