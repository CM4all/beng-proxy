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
#include "lb_hmonitor.h"
#include "ssl_init.h"
#include "child.h"

#include <daemon/daemonize.h>

#include <assert.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>

#include <event.h>

static const struct timeval launch_worker_now = {
    .tv_sec = 0,
    .tv_usec = 10000,
};

static const struct timeval launch_worker_delayed = {
    .tv_sec = 10,
    .tv_usec = 0,
};

static bool is_watchdog;
static pid_t worker_pid;
static struct event launch_worker_event;

static void
worker_callback(int status, void *ctx)
{
    struct lb_instance *instance = ctx;

    int exit_status = WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        fprintf(stderr, "worker %d died from signal %d%s\n",
                worker_pid, WTERMSIG(status),
                WCOREDUMP(status) ? " (core dumped)" : "");
    else if (exit_status == 0)
        fprintf(stderr, "worker %d exited with success\n",
                worker_pid);
    else
        fprintf(stderr, "worker %d exited with status %d\n",
                worker_pid, exit_status);

    worker_pid = 0;

    if (!instance->should_exit)
        evtimer_add(&launch_worker_event, &launch_worker_delayed);
}

static void
launch_worker_callback(int fd __attr_unused, short event __attr_unused,
                       void *ctx)
{
    assert(is_watchdog);
    assert(worker_pid <= 0);

    struct lb_instance *instance = ctx;

    worker_pid = fork();
    if (worker_pid < 0) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
        evtimer_add(&launch_worker_event, &launch_worker_delayed);
        return;
    }

    if (worker_pid == 0) {
        event_reinit(instance->event_base);
        all_listeners_event_add(instance);
        return;
    }

    child_register(worker_pid, worker_callback, instance);
}

static void
exit_event_callback(int fd __attr_unused, short event __attr_unused, void *ctx)
{
    struct lb_instance *instance = ctx;

    if (instance->should_exit)
        return;

    instance->should_exit = true;
    deinit_signals(instance);

    if (is_watchdog && worker_pid > 0)
        kill(worker_pid, SIGTERM);

    children_shutdown();

    if (is_watchdog)
        evtimer_del(&launch_worker_event);

    deinit_all_listeners(instance);
    deinit_all_controls(instance);

    while (!list_empty(&instance->connections))
        lb_connection_close((struct lb_connection*)instance->connections.next);

    lb_hmonitor_deinit();

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

    lb_hmonitor_init(instance.pool);

    ssl_global_init();

    if (instance.cmdline.enable_splice)
        direct_global_init();

    instance.event_base = event_init();

    list_init(&instance.controls);
    list_init(&instance.listeners);
    list_init(&instance.connections);

    init_signals(&instance);

    children_init(instance.pool);

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

    if (!init_all_controls(&instance, &error)) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    if (!init_all_listeners(&instance, &error)) {
        deinit_all_controls(&instance);
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

    if (instance.cmdline.num_workers > 0) {
        /* watchdog */

        all_listeners_event_del(&instance);

        is_watchdog = true;
        evtimer_set(&launch_worker_event, launch_worker_callback, &instance);
        evtimer_add(&launch_worker_event, &launch_worker_now);
    }

    event_dispatch();

    /* cleanup */

    children_shutdown();

    log_global_deinit();

    bulldog_deinit();
    failure_deinit();

    deinit_all_listeners(&instance);
    deinit_all_controls(&instance);

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
