#include "listener.h"
#include "http-server.h"
#include "duplex.h"
#include "direct.h"
#include "tpool.h"

#include <stdio.h>
#include <stdlib.h>
#include <event.h>

struct connection {
    struct list_head siblings;

    struct pool *pool;

    struct instance *instance;

    struct http_server_connection *http;
};

struct instance {
    struct pool *pool;

    struct listener *listener;

    struct list_head connections;
};

/*
 * http_server_connection_handler
 *
 */

static void
my_http_request(struct http_server_request *request, void *ctx,
                G_GNUC_UNUSED struct async_operation_ref *async_ref)
{
    struct connection *connection = ctx;
    (void)connection;

    http_server_send_message(request, HTTP_STATUS_OK, "Hello world!");
}

static void
my_http_error(GError *error, void *ctx)
{
    (void)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);
}

static void
my_http_free(void *ctx)
{
    struct connection *connection = ctx;

    list_remove(&connection->siblings);
    pool_unref(connection->pool);
}

static const struct http_server_connection_handler my_http_handler = {
    .request = my_http_request,
    .error = my_http_error,
    .free = my_http_free,
};

/*
 * listener_handler
 *
 */

static void
my_listener_connected(int fd,
                      G_GNUC_UNUSED const struct sockaddr *address,
                      G_GNUC_UNUSED size_t address_length,
                      void *ctx)
{
    struct instance *instance = ctx;

    struct pool *pool = pool_new_linear(instance->pool, "connection", 8192);
    struct connection *connection = p_malloc(pool, sizeof(*connection));
    list_add(&connection->siblings, &instance->connections);
    connection->pool = pool;
    connection->instance = instance;

    http_server_connection_new(pool, fd, ISTREAM_TCP, NULL, 0, NULL,
                               &my_http_handler, connection,
                               &connection->http);
}

static void
my_listener_error(GError *error, void *ctx)
{
    (void)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);
}

static const struct listener_handler my_listener_handler = {
    .connected = my_listener_connected,
    .error = my_listener_error,
};

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s PORT\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *endptr;
    unsigned port = strtoul(argv[1], &endptr, 0);
    if (port == 0 || *endptr != 0) {
        fprintf(stderr, "Invalid port number\n");
        return EXIT_FAILURE;
    }

    direct_global_init();

    struct event_base *event_base = event_init();

    struct instance instance;
    instance.pool = pool_new_libc(NULL, "root");
    list_init(&instance.connections);

    tpool_init(instance.pool);

    GError *error = NULL;
    instance.listener = listener_tcp_port_new(instance.pool, port,
                                              &my_listener_handler, &instance,
                                              &error);
    if (error != NULL) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
    } else
        event_dispatch();

    tpool_deinit();
    pool_unref(instance.pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();
}
