/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server.h"
#include "pool.h"
#include "instance.h"
#include "connection.h"
#include "session.h"

#include <daemon/daemonize.h>

#include <assert.h>
#include <unistd.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <stdio.h>

#include <event.h>

#ifndef NDEBUG
int debug_mode = 0;
#endif

static void
exit_event_callback(int fd, short event, void *ctx)
{
    struct instance *instance = (struct instance*)ctx;

    (void)fd;
    (void)event;

    if (instance->should_exit)
        return;

    instance->should_exit = 1;
    deinit_signals(instance);

    if (instance->listener != NULL)
        listener_free(&instance->listener);

    while (!list_empty(&instance->connections))
        remove_connection((struct client_connection*)instance->connections.next);

    event_del(&instance->child_event);
    kill_children(instance);

    session_manager_deinit();
}

void
init_signals(struct instance *instance)
{
    signal(SIGPIPE, SIG_IGN);

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

void
deinit_signals(struct instance *instance)
{
    event_del(&instance->sigterm_event);
    event_del(&instance->sigint_event);
    event_del(&instance->sigquit_event);
}

int main(int argc, char **argv)
{
    int ret;
    int attr_unused ref;
    static struct instance instance;

#ifndef NDEBUG
    if (geteuid() != 0)
        debug_mode = 1;
#endif

    /* configuration */

    instance.config.document_root = "/var/www";

    parse_cmdline(&instance.config, argc, argv);

    /* initialize */

    instance.event_base = event_init();

    list_init(&instance.connections);
    list_init(&instance.children);
    instance.pool = pool_new_libc(NULL, "global");

    init_signals(&instance);

    session_manager_init(instance.pool);

    ret = listener_tcp_port_new(instance.pool,
                                8080, &http_listener_callback, &instance,
                                &instance.listener);
    if (ret < 0) {
        perror("listener_tcp_port_new() failed");
        exit(2);
    }

    /* daemonize */

    ret = daemonize();
    if (ret < 0)
        exit(2);

    /* create worker processes */

    if (instance.config.num_workers > 0) {
        pid_t pid;

        /* the master process shouldn't work */
        listener_event_del(instance.listener);

        while (instance.num_children < instance.config.num_workers) {
            pid = create_child(&instance);
            if (pid <= 0)
                break;
        }
    }

    /* main loop */

    event_dispatch();

    /* cleanup */

    if (instance.listener != NULL)
        listener_free(&instance.listener);

#ifndef PROFILE
    event_base_free(instance.event_base);
#endif

    ref = pool_unref(instance.pool);
    assert(ref == 0);
    pool_commit();

    pool_recycler_clear();
}
