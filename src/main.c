/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server.h"
#include "pool.h"
#include "instance.h"

#include <sys/signal.h>
#include <stdlib.h>
#include <stdio.h>

#include <event.h>

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
                                8080, &http_listener_callback, &instance,
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
