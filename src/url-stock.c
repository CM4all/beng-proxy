/*
 * HTTP client connection pooling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "url-stock.h"
#include "stock.h"
#include "async.h"
#include "client-socket.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/un.h>

struct url_connection {
    struct stock_item stock_item;
    const char *uri;

    struct async_operation create_operation;

    struct async_operation_ref client_socket;
    http_client_connection_t http;
};


static int
getaddrinfo_helper(const char *host_and_port, int default_port,
                   const struct addrinfo *hints,
                   struct addrinfo **aip) {
    const char *colon, *host, *port;
    char buffer[256];

    colon = strchr(host_and_port, ':');
    if (colon == NULL) {
        snprintf(buffer, sizeof(buffer), "%d", default_port);

        host = host_and_port;
        port = buffer;
    } else {
        size_t len = colon - host_and_port;

        if (len >= sizeof(buffer)) {
            errno = ENAMETOOLONG;
            return EAI_SYSTEM;
        }

        memcpy(buffer, host_and_port, len);
        buffer[len] = 0;

        host = buffer;
        port = colon + 1;
    }

    if (strcmp(host, "*") == 0)
        host = "0.0.0.0";

    return getaddrinfo(host, port, hints, aip);
}


/*
 * async operation
 *
 */

static struct url_connection *
async_to_url_connection(struct async_operation *ao)
{
    return (struct url_connection*)(((char*)ao) - offsetof(struct url_connection, create_operation));
}

static void
url_create_abort(struct async_operation *ao)
{
    struct url_connection *connection = async_to_url_connection(ao);

    assert(connection != NULL);
    assert(async_ref_defined(&connection->client_socket));

    async_abort(&connection->client_socket);
    stock_available(&connection->stock_item, 0);
}

static struct async_operation_class url_create_operation = {
    .abort = url_create_abort,
};


/*
 * http_client connection handler
 *
 */

static void
url_http_connection_idle(void *ctx)
{
    struct url_connection *connection = ctx;

    stock_put(&connection->stock_item, 0);
}

static void
url_http_connection_free(void *ctx)
{
    struct url_connection *connection = ctx;

    if (connection->http == NULL)
        /* we are being called through url_stock_destroy() which means
           that the stock item is already removed */
        return;

    if (stock_item_is_idle(&connection->stock_item))
        stock_del(&connection->stock_item);
    else
        stock_put(&connection->stock_item, 1);
}

static const struct http_client_connection_handler url_http_connection_handler = {
    .idle = url_http_connection_idle,
    .free = url_http_connection_free,
};


/*
 * client_socket callback
 *
 */

static void
url_client_socket_callback(int fd, int err, void *ctx)
{
    struct url_connection *connection = ctx;

    async_ref_clear(&connection->client_socket);

    if (err == 0) {
        assert(fd >= 0);

        connection->http = http_client_connection_new(connection->stock_item.pool, fd,
                                                      &url_http_connection_handler, connection);
        stock_available(&connection->stock_item, 1);
    } else {
        daemon_log(1, "failed to connect to '%s': %s\n",
                   connection->uri, strerror(err));

        stock_available(&connection->stock_item, 0);
    }
}


/*
 * stock class
 *
 */

static pool_t
url_stock_pool(void *ctx, pool_t parent, const char *uri)
{
    (void)ctx;
    (void)uri;

    return pool_new_linear(parent, "url_stock", 8192);
}

static void
url_stock_create(void *ctx, struct stock_item *item, const char *uri,
                 struct async_operation_ref *async_ref)
{
    struct url_connection *connection = (struct url_connection *)item;
    int ret;

    assert(uri != NULL);

    (void)ctx;

    async_ref_clear(&connection->client_socket);
    connection->http = NULL;

    async_init(&connection->create_operation,
               &url_create_operation);
    async_ref_set(async_ref, &connection->create_operation);

    connection->uri = uri;

    if (uri[0] != '/') {
        /* HTTP over TCP */
        struct addrinfo hints, *ai;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = PF_INET;
        hints.ai_socktype = SOCK_STREAM;

        /* XXX make this asynchronous */
        ret = getaddrinfo_helper(uri, 80, &hints, &ai);
        if (ret != 0) {
            daemon_log(1, "failed to resolve proxy host name\n");
            stock_available(item, 0);
            return;
        }

        client_socket_new(connection->stock_item.pool,
                          PF_INET, SOCK_STREAM, 0,
                          ai->ai_addr, ai->ai_addrlen,
                          url_client_socket_callback, connection,
                          &connection->client_socket);
        freeaddrinfo(ai);
    } else {
        /* HTTP over Unix socket */
        size_t path_length;
        struct sockaddr_un sun;

        path_length = strlen(uri);

        if (path_length >= sizeof(sun.sun_path)) {
            daemon_log(1, "client_socket_new() failed: unix socket path is too long\n");
            stock_available(item, 0);
            return;
        }

        sun.sun_family = AF_UNIX;
        memcpy(sun.sun_path, uri, path_length + 1);

        client_socket_new(connection->stock_item.pool,
                          PF_UNIX, SOCK_STREAM, 0,
                          (const struct sockaddr*)&sun, sizeof(sun),
                          url_client_socket_callback, connection,
                          &connection->client_socket);
    }
}

static int
url_stock_validate(void *ctx, struct stock_item *item)
{
    struct url_connection *connection = (struct url_connection *)item;

    (void)ctx;

    return connection->http != NULL;
}

static void
url_stock_destroy(void *ctx, struct stock_item *item)
{
    struct url_connection *connection = (struct url_connection *)item;

    (void)ctx;

    if (async_ref_defined(&connection->client_socket))
        async_abort(&connection->client_socket);
    else if (connection->http != NULL)
        http_client_connection_free(&connection->http);
}

static struct stock_class url_stock_class = {
    .item_size = sizeof(struct url_connection),
    .pool = url_stock_pool,
    .create = url_stock_create,
    .validate = url_stock_validate,
    .destroy = url_stock_destroy,
};


/*
 * interface
 *
 */

struct hstock *
url_hstock_new(pool_t pool)
{
    return hstock_new(pool, &url_stock_class, NULL);
}

http_client_connection_t
url_stock_item_get(struct stock_item *item)
{
    struct url_connection *connection = (struct url_connection *)item;

    assert(item != NULL);

    return connection->http;
}
