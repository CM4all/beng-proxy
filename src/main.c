/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "listener.h"
#include "http-server.h"
#include "pool.h"

#include <sys/types.h>
#include <sys/signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <event.h>

struct instance {
    pool_t pool;
    listener_t listener;
    int should_exit;
    struct event sigterm_event, sigint_event, sigquit_event;
};

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
}

static void
my_http_server_callback(struct http_server_request *request,
                        /*const void *body, size_t body_length,*/
                        void *ctx)
{
    (void)request;
    (void)ctx;

    printf("in my_http_server_callback()\n");

    http_server_send_message(request->connection, HTTP_STATUS_OK, "Hello, world!");
}

static void
my_listener_callback(int fd,
                     const struct sockaddr *addr, socklen_t addrlen,
                     void *ctx)
{
    int ret;
    http_server_connection_t connection;

    (void)addr;
    (void)addrlen;
    (void)ctx;

    printf("client %d\n", fd);

    ret = http_server_connection_new(fd, my_http_server_callback, NULL, &connection);
    if (ret < 0)
        close(fd);
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

    instance.pool = pool_new_libc(NULL, "global");

    setup_signals(&instance);

    ret = listener_tcp_port_new(instance.pool,
                                8080, &my_listener_callback, NULL,
                                &instance.listener);
    if (ret < 0) {
        perror("listener_tcp_port_new() failed");
        exit(2);
    }

    event_dispatch();

    if (instance.listener != NULL)
        listener_free(&instance.listener);

    pool_destroy(instance.pool);
}
