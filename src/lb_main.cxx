/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "tpool.h"
#include "direct.h"
#include "lb_instance.hxx"
#include "lb_setup.hxx"
#include "lb_connection.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "hstock.hxx"
#include "stock.hxx"
#include "global.h"
#include "failure.hxx"
#include "bulldog.h"
#include "balancer.hxx"
#include "pipe_stock.hxx"
#include "log-glue.h"
#include "lb_config.hxx"
#include "lb_hmonitor.hxx"
#include "ssl_init.hxx"
#include "child_manager.hxx"
#include "thread_pool.hxx"
#include "fb_pool.hxx"
#include "capabilities.hxx"
#include "isolate.hxx"
#include "util/Error.hxx"

#include <daemon/log.h>
#include <daemon/daemonize.h>

#include <assert.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>

#ifdef __linux
#include <sys/prctl.h>

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif
#endif

#include <event.h>

static constexpr cap_value_t cap_keep_list[1] = {
    /* keep the NET_RAW capability to be able to to use the socket
       option IP_TRANSPARENT */
    CAP_NET_RAW,
};

static constexpr struct timeval launch_worker_now = {
    0,
    10000,
};

static constexpr struct timeval launch_worker_delayed = {
    10,
    0,
};

static bool is_watchdog;
static pid_t worker_pid;
static struct event launch_worker_event;

static void
worker_callback(int status, void *ctx)
{
    struct lb_instance *instance = (struct lb_instance *)ctx;

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
launch_worker_callback(int fd gcc_unused, short event gcc_unused,
                       void *ctx)
{
    assert(is_watchdog);
    assert(worker_pid <= 0);

    struct lb_instance *instance = (struct lb_instance *)ctx;

    /* in libevent 2.0.16, it is necessary to re-add all EV_SIGNAL
       events after forking; this bug is not present in 1.4.13 and
       2.0.19 */
    deinit_signals(instance);
    children_event_del();

    worker_pid = fork();
    if (worker_pid < 0) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));

        init_signals(instance);
        children_event_add();

        evtimer_add(&launch_worker_event, &launch_worker_delayed);
        return;
    }

    if (worker_pid == 0) {
        event_reinit(instance->event_base);
        init_signals(instance);

        children_init(instance->pool);
        all_listeners_event_add(instance);

        enable_all_controls(instance);

        /* run monitors only in the worker process */
        lb_hmonitor_enable();
        return;
    }

    init_signals(instance);
    children_event_add();

    child_register(worker_pid, "worker", worker_callback, instance);
}

static void
shutdown_callback(void *ctx)
{
    struct lb_instance *instance = (struct lb_instance *)ctx;

    if (instance->should_exit)
        return;

    instance->should_exit = true;
    deinit_signals(instance);
    thread_pool_stop();

    if (is_watchdog && worker_pid > 0)
        kill(worker_pid, SIGTERM);

    children_shutdown();

    thread_pool_join();
    thread_pool_deinit();

    if (is_watchdog)
        evtimer_del(&launch_worker_event);

    deinit_all_controls(instance);

    while (!instance->connections.empty())
        lb_connection_close(&instance->connections.front());

    deinit_all_listeners(instance);

    lb_hmonitor_deinit();

    pool_commit();

    if (instance->tcp_stock != NULL)
        hstock_free(instance->tcp_stock);

    if (instance->balancer != NULL)
        balancer_free(instance->balancer);

    if (instance->pipe_stock != NULL)
        stock_free(instance->pipe_stock);

    fb_pool_disable();

    pool_commit();
}

static void
reload_event_callback(int fd gcc_unused, short event gcc_unused,
                      void *ctx)
{
    struct lb_instance *instance = (struct lb_instance *)ctx;

    daemonize_reopen_logfile();

    unsigned n_ssl_sessions = instance->FlushSSLSessionCache(LONG_MAX);
    daemon_log(3, "flushed %u SSL sessions\n", n_ssl_sessions);

    fb_pool_compress();
}

void
init_signals(struct lb_instance *instance)
{
    signal(SIGPIPE, SIG_IGN);

    shutdown_listener_init(&instance->shutdown_listener,
                           shutdown_callback, instance);

    event_set(&instance->sighup_event, SIGHUP, EV_SIGNAL|EV_PERSIST,
              reload_event_callback, instance);
    event_add(&instance->sighup_event, NULL);
}

void
deinit_signals(struct lb_instance *instance)
{
    shutdown_listener_deinit(&instance->shutdown_listener);
    event_del(&instance->sighup_event);
}

int main(int argc, char **argv)
{
    Error error2;
    int ret;
    int gcc_unused ref;
    static struct lb_instance instance;
    instance.cmdline.config_path = "/etc/cm4all/beng/lb.conf";
    instance.cmdline.max_connections = 8192;
    instance.cmdline.tcp_stock_limit = 256;

#ifdef HAVE_OLD_GTHREAD
    /* deprecated in GLib 2.32 */
    g_thread_init(NULL);
#endif

    instance.pool = pool_new_libc(NULL, "global");
    tpool_init(instance.pool);

    /* configuration */

    parse_cmdline(&instance.cmdline, instance.pool, argc, argv);

    GError *error = NULL;
    instance.config = lb_config_load(instance.pool,
                                     instance.cmdline.config_path,
                                     &error);
    if (instance.config == NULL) {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    if (instance.cmdline.check)
        return EXIT_SUCCESS;

    /* initialize */

    lb_hmonitor_init(instance.pool);

    ssl_global_init();

    direct_global_init();

    instance.event_base = event_init();
    fb_pool_init(true);

    list_init(&instance.controls);

    init_signals(&instance);

    children_init(instance.pool);

    instance.balancer = balancer_new(*instance.pool);
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

    if (!init_all_listeners(instance, error2)) {
        deinit_all_controls(&instance);
        fprintf(stderr, "%s\n", error2.GetMessage());
        return EXIT_FAILURE;
    }

    /* daemonize */

    ret = daemonize();
    if (ret < 0)
        exit(2);

    /* launch the access logger */

    if (!log_global_init(instance.cmdline.access_logger))
        return EXIT_FAILURE;

    /* daemonize II */

    if (daemon_user_defined(&instance.cmdline.user))
        capabilities_pre_setuid();

    if (daemon_user_set(&instance.cmdline.user) < 0)
        return EXIT_FAILURE;

    isolate_from_filesystem();

    if (daemon_user_defined(&instance.cmdline.user))
        capabilities_post_setuid(cap_keep_list, G_N_ELEMENTS(cap_keep_list));

#ifdef __linux
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
#endif

    /* main loop */

    if (instance.cmdline.num_workers > 0) {
        /* watchdog */

        all_listeners_event_del(&instance);

        is_watchdog = true;
        evtimer_set(&launch_worker_event, launch_worker_callback, &instance);
        evtimer_add(&launch_worker_event, &launch_worker_now);
    } else {
        /* this is already the worker process: enable monitors here */
        lb_hmonitor_enable();
    }

    event_dispatch();

    /* cleanup */

    children_shutdown();

    log_global_deinit();

    bulldog_deinit();
    failure_deinit();

    deinit_all_listeners(&instance);
    deinit_all_controls(&instance);

    fb_pool_deinit();

    event_base_free(instance.event_base);

    tpool_deinit();
    delete instance.config;

    ref = pool_unref(instance.pool);
    assert(ref == 0);
    pool_commit();

    pool_recycler_clear();

    ssl_global_deinit();

    daemonize_cleanup();

    direct_global_deinit();
}
