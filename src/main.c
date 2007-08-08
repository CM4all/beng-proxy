/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "listener.h"
#include "http-server.h"
#include "pool.h"
#include "list.h"

#include <sys/types.h>
#include <sys/signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <event.h>

struct client_connection {
    struct list_head siblings;
    pool_t pool;
    http_server_connection_t http;
};

struct instance {
    pool_t pool;
    listener_t listener;
    struct list_head connections;
    int should_exit;
    struct event sigterm_event, sigint_event, sigquit_event;
};

static void
remove_connection(struct client_connection *connection)
{
    list_remove(&connection->siblings);

    if (connection->http != NULL)
        http_server_connection_free(&connection->http);

    pool_unref(connection->pool);
}

static void
exit_event_callback(int fd, short event, void *ctx)
{
    struct instance *instance = (struct instance*)ctx;

    (void)fd;
    (void)event;

    if (instance->should_exit)
        return;

    instance->should_exit = 1;
    event_del(&instance->sigterm_event);
    event_del(&instance->sigint_event);
    event_del(&instance->sigquit_event);

    if (instance->listener != NULL)
        listener_free(&instance->listener);

    while (!list_empty(&instance->connections))
        remove_connection((struct client_connection*)instance->connections.next);
}

static void
my_http_server_callback(struct http_server_request *request,
                        /*const void *body, size_t body_length,*/
                        void *ctx)
{
    struct client_connection *connection = ctx;

    (void)request;
    (void)connection;

    printf("in my_http_server_callback()\n");

    http_server_send_message(request->connection, HTTP_STATUS_OK, "Hello, world!");
}

static void
my_listener_callback(int fd,
                     const struct sockaddr *addr, socklen_t addrlen,
                     void *ctx)
{
    struct instance *instance = (struct instance*)ctx;
    int ret;
    pool_t pool;
    struct client_connection *connection;

    (void)addr;
    (void)addrlen;
    (void)ctx;

    printf("client %d\n", fd);

    pool = pool_new_linear(instance->pool, "client_connection", 8192);
    connection = p_calloc(pool, sizeof(*connection));
    connection->pool = pool;

    list_add(&connection->siblings, &instance->connections);

    ret = http_server_connection_new(pool, fd,
                                     my_http_server_callback, connection,
                                     &connection->http);
    if (ret < 0) {
        close(fd);
        remove_connection(connection);
    }
}

static void
setup_signals(struct instance *instance)
{
    event_set(&instance->sigterm_event, SIGTERM, EV_SIGNAL|EV_PERSIST,
              exit_event_callback, instance);
    event_add(&instance->sigterm_event, NULL);

    event_set(&instance->sigint_event, SIGINT, EV_SIGNAL|EV_PERSIST,
              exit_event_callback, instance);
    event_add(&instance->sigint_event, NULL);

    event_set(&instance->sigquit_event, SIGQUIT, EV_SIGNAL|EV_PERSIST,
              exit_event_callback, instance);
    event_add(&instance->sigquit_event, NULL);
}

int main(int argc, char **argv)
{
    int ret;
    static struct instance instance;

    (void)argc;
    (void)argv;

    event_init();

    list_init(&instance.connections);
    instance.pool = pool_new_libc(NULL, "global");

    setup_signals(&instance);

    ret = listener_tcp_port_new(instance.pool,
                                8080, &my_listener_callback, &instance,
                                &instance.listener);
    if (ret < 0) {
        perror("listener_tcp_port_new() failed");
        exit(2);
    }

    event_dispatch();

    if (instance.listener != NULL)
        listener_free(&instance.listener);

    pool_unref(instance.pool);
}
