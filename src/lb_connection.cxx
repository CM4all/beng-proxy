/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_connection.hxx"
#include "lb_log.hxx"
#include "lb_config.hxx"
#include "lb_instance.hxx"
#include "lb_http.hxx"
#include "lb_tcp.hxx"
#include "strmap.h"
#include "http-server.h"
#include "drop.h"
#include "fd_util.h"
#include "ssl_filter.hxx"
#include "pool.h"

#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * lb_tcp_handler
 *
 */

static void
tcp_eof(void *ctx)
{
    lb_connection *connection = (lb_connection *)ctx;

    lb_connection_close(connection);
}

static void
tcp_error(const char *prefix, const char *error, void *ctx)
{
    lb_connection *connection = (lb_connection *)ctx;

    lb_connection_log_error(3, connection, prefix, error);
    lb_connection_close(connection);
}

static void
tcp_errno(const char *prefix, int error, void *ctx)
{
    lb_connection *connection = (lb_connection *)ctx;

    lb_connection_log_errno(3, connection, prefix, error);
    lb_connection_close(connection);
}

static void
tcp_gerror(const char *prefix, GError *error, void *ctx)
{
    lb_connection *connection = (lb_connection *)ctx;

    lb_connection_log_gerror(3, connection, prefix, error);
    g_error_free(error);
    lb_connection_close(connection);
}

static const struct lb_tcp_handler tcp_handler = {
    .eof = tcp_eof,
    .error = tcp_error,
    ._errno = tcp_errno,
    .gerror = tcp_gerror,
};

/*
 * public
 *
 */

struct lb_connection *
lb_connection_new(struct lb_instance *instance,
                  const struct lb_listener_config *listener,
                  struct ssl_factory *ssl_factory, struct notify *notify,
                  int fd, const struct sockaddr *addr, size_t addrlen)
{
    /* determine the local socket address */
    struct sockaddr_storage local_address;
    socklen_t local_address_length = sizeof(local_address);
    if (getsockname(fd, (struct sockaddr *)&local_address,
                    &local_address_length) < 0)
        local_address_length = 0;

    struct pool *pool = pool_new_linear(instance->pool, "client_connection",
                                        2048);
    pool_set_major(pool);

    struct lb_connection *connection =
        (struct lb_connection *)p_malloc(pool, sizeof(*connection));
    connection->pool = pool;
    connection->instance = instance;
    connection->listener = listener;

    enum istream_direct fd_type = ISTREAM_TCP;

    if (ssl_factory != NULL) {
        int fds[2];
        if (socketpair_cloexec_nonblock(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
            close(fd);
            pool_unref(pool);
            return NULL;
        }

        GError *error = NULL;
        connection->ssl_filter = ssl_filter_new(pool, ssl_factory,
                                                fd, fds[0], notify,
                                                &error);
        if (connection->ssl_filter == NULL) {
            close(fd);
            close(fds[0]);
            close(fds[1]);
            lb_connection_log_gerror(1, connection, "SSL", error);
            g_error_free(error);
            pool_unref(pool);
            return NULL;
        }

        fd = fds[1];
        fd_type = ISTREAM_SOCKET;
    } else
        connection->ssl_filter = NULL;

    list_add(&connection->siblings, &instance->connections);
    ++connection->instance->num_connections;

    switch (listener->destination.GetProtocol()) {
    case LB_PROTOCOL_HTTP:
        http_server_connection_new(pool, fd, fd_type,
                                   local_address_length > 0
                                   ? (const struct sockaddr *)&local_address
                                   : NULL,
                                   local_address_length,
                                   addr, addrlen,
                                   false,
                                   &lb_http_connection_handler,
                                   connection,
                                   &connection->http);
        break;

    case LB_PROTOCOL_TCP:
        lb_tcp_new(connection->pool, instance->pipe_stock,
                   fd, fd_type, addr,
                   connection->listener->destination.cluster->address_list,
                   *connection->instance->balancer,
                   &tcp_handler, connection,
                   &connection->tcp);
        break;
    }

    return connection;
}

void
lb_connection_remove(struct lb_connection *connection)
{
    assert(connection != NULL);
    assert(connection->instance != NULL);
    assert(connection->instance->num_connections > 0);

    list_remove(&connection->siblings);
    --connection->instance->num_connections;

    if (connection->ssl_filter != NULL)
        ssl_filter_free(connection->ssl_filter);

    struct pool *pool = connection->pool;
    pool_trash(pool);
    pool_unref(pool);
}

void
lb_connection_close(struct lb_connection *connection)
{
    switch (connection->listener->destination.GetProtocol()) {
    case LB_PROTOCOL_HTTP:
        assert(connection->http != NULL);
        http_server_connection_close(connection->http);
        break;

    case LB_PROTOCOL_TCP:
        lb_tcp_close(connection->tcp);
        break;
    }

    lb_connection_remove(connection);
}
