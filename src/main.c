/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tpool.h"
#include "instance.h"
#include "connection.h"
#include "session.h"
#include "translate.h"
#include "url-stock.h"
#include "stock.h"
#include "http-cache.h"

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
        close_connection((struct client_connection*)instance->connections.next);

    event_del(&instance->child_event);
    kill_children(instance);

    session_manager_deinit();

    if (instance->translate_stock != NULL)
        stock_free(&instance->translate_stock);

    if (instance->http_cache != NULL) {
        http_cache_close(instance->http_cache);
        instance->http_cache = NULL;
    }

    if (instance->http_client_stock != NULL)
        hstock_free(&instance->http_client_stock);
}

static void
reload_event_callback(int fd __attr_unused, short event __attr_unused,
                      void *ctx __attr_unused)
{
    daemonize_reopen_logfile();
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

    event_set(&instance->sighup_event, SIGHUP, EV_SIGNAL|EV_PERSIST,
              reload_event_callback, instance);
    event_add(&instance->sighup_event, NULL);
}

void
deinit_signals(struct instance *instance)
{
    event_del(&instance->sigterm_event);
    event_del(&instance->sigint_event);
    event_del(&instance->sigquit_event);
    event_del(&instance->sighup_event);
}

int main(int argc, char **argv)
{
    int ret;
    int __attr_unused ref;
    static struct instance instance = {
        .config = {
            .max_connnections = 1024,
        },
    };

#ifndef NDEBUG
    if (geteuid() != 0)
        debug_mode = 1;
#endif

    /* configuration */

    if (debug_mode)
        instance.config.port = 8080;
    else
        instance.config.port = 80;
    instance.config.document_root = "/var/www";

    parse_cmdline(&instance.config, argc, argv);

    /* initialize */

    instance.event_base = event_init();

    list_init(&instance.connections);
    list_init(&instance.children);
    instance.pool = pool_new_libc(NULL, "global");
    tpool_init(instance.pool);

    init_signals(&instance);

    session_manager_init(instance.pool);

    ret = listener_tcp_port_new(instance.pool,
                                instance.config.port,
                                &http_listener_callback, &instance,
                                &instance.listener);
    if (ret < 0) {
        perror("listener_tcp_port_new() failed");
        exit(2);
    }

    instance.translate_stock = translate_stock_new(instance.pool,
                                                   instance.config.translation_socket);
    instance.http_client_stock = url_hstock_new(instance.pool);
    instance.http_cache = http_cache_new(instance.pool, 1024 * 1024,
                                         instance.http_client_stock);

    /* daemonize */

#ifndef PROFILE
    ret = daemonize();
    if (ret < 0)
        exit(2);
#endif

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

    tpool_deinit();
    ref = pool_unref(instance.pool);
    assert(ref == 0);
    pool_commit();

    pool_recycler_clear();

    daemonize_cleanup();
}
