/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tpool.h"
#include "direct.h"
#include "lb_instance.h"
#include "lb_setup.h"
#include "lb_connection.h"
#include "tcp-stock.h"
#include "tcp-balancer.h"
#include "stock.h"
#include "global.h"
#include "failure.h"
#include "bulldog.h"
#include "balancer.h"
#include "listener.h"
#include "pipe-stock.h"
#include "log-glue.h"
#include "lb_config.h"
#include "ssl_init.h"

#include <daemon/daemonize.h>

#include <assert.h>
#include <unistd.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>

#include <event.h>

static void
exit_event_callback(int fd __attr_unused, short event __attr_unused, void *ctx)
{
    struct lb_instance *instance = ctx;

    if (instance->should_exit)
        return;

    instance->should_exit = true;
    deinit_signals(instance);

    deinit_all_listeners(instance);

    while (!list_empty(&instance->connections))
        lb_connection_close((struct lb_connection*)instance->connections.next);

    pool_commit();

    if (instance->tcp_stock != NULL)
        hstock_free(instance->tcp_stock);

    if (instance->balancer != NULL)
        balancer_free(instance->balancer);

    if (instance->pipe_stock != NULL)
        stock_free(instance->pipe_stock);

    pool_commit();
}

static void
reload_event_callback(int fd __attr_unused, short event __attr_unused,
                      void *ctx)
{
    struct instance *instance = (struct instance*)ctx;

    (void)instance;

    daemonize_reopen_logfile();
}

void
init_signals(struct lb_instance *instance)
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
deinit_signals(struct lb_instance *instance)
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
    static struct lb_instance instance = {
        .cmdline = {
            .max_connections = 8192,
            .tcp_stock_limit = 256,
            .enable_splice = true,
        },
    };

    instance.pool = pool_new_libc(NULL, "global");
    tpool_init(instance.pool);

    /* configuration */

    parse_cmdline(&instance.cmdline, instance.pool, argc, argv);

    GError *error = NULL;
    instance.config = lb_config_load(instance.pool,
                                     "/etc/cm4all/beng/lb.conf",
                                     &error);
    if (instance.config == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    /* initialize */

    ssl_global_init();

    if (instance.cmdline.enable_splice)
        direct_global_init();

    instance.event_base = event_init();

    list_init(&instance.listeners);
    list_init(&instance.connections);

    init_signals(&instance);

    instance.balancer = balancer_new(instance.pool);
    instance.tcp_stock = tcp_stock_new(instance.pool,
                                       instance.cmdline.tcp_stock_limit);
    instance.tcp_balancer = tcp_balancer_new(instance.pool, instance.tcp_stock,
                                             instance.balancer);

    instance.pipe_stock = pipe_stock_new(instance.pool);

    failure_init(instance.pool);
    bulldog_init(instance.cmdline.bulldog_path);

    global_tcp_stock = instance.tcp_stock;
    global_pipe_stock = instance.pipe_stock;

    if (!init_all_listeners(&instance, &error)) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    if (!log_global_init(instance.cmdline.access_logger))
        return EXIT_FAILURE;

    /* daemonize */

#ifndef PROFILE
    ret = daemonize();
    if (ret < 0)
        exit(2);
#endif

    /* main loop */

    event_dispatch();

    /* cleanup */

    log_global_deinit();

    bulldog_deinit();
    failure_deinit();

    deinit_all_listeners(&instance);

#ifndef PROFILE
    event_base_free(instance.event_base);
#endif

    tpool_deinit();
    ref = pool_unref(instance.config->pool);
    assert(ref == 0);

    ref = pool_unref(instance.pool);
    assert(ref == 0);
    pool_commit();

    pool_recycler_clear();

    daemonize_cleanup();

    if (instance.cmdline.enable_splice)
        direct_global_deinit();
}
