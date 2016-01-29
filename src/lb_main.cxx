/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "direct.hxx"
#include "lb_instance.hxx"
#include "lb_check.hxx"
#include "lb_setup.hxx"
#include "lb_connection.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "stock/MapStock.hxx"
#include "failure.hxx"
#include "bulldog.h"
#include "balancer.hxx"
#include "pipe_stock.hxx"
#include "log-glue.h"
#include "lb_config.hxx"
#include "lb_hmonitor.hxx"
#include "ssl/ssl_init.hxx"
#include "child_manager.hxx"
#include "pool.hxx"
#include "thread_pool.hxx"
#include "fb_pool.hxx"
#include "capabilities.hxx"
#include "isolate.hxx"
#include "system/SetupProcess.hxx"
#include "util/PrintException.hxx"
#include "util/Error.hxx"
#include "util/Macros.hxx"

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
        instance->launch_worker_event.Add(launch_worker_delayed);
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

        instance->launch_worker_event.Add(launch_worker_delayed);
        return;
    }

    if (worker_pid == 0) {
        is_watchdog = false;

        instance->event_base.Reinit();
        init_signals(instance);

        children_init();
        all_listeners_event_add(instance);

        enable_all_controls(instance);

        /* run monitors only in the worker process */
        lb_hmonitor_enable();

        instance->ConnectCertCaches();
        return;
    }

    init_signals(instance);
    children_event_add();

    child_register(worker_pid, "worker", worker_callback, instance);
}

void
lb_instance::ShutdownCallback(void *ctx)
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

    if (is_watchdog)
        instance->launch_worker_event.Cancel();

    deinit_all_controls(instance);

    if (!is_watchdog)
        instance->DisconnectCertCaches();

    while (!instance->connections.empty())
        lb_connection_close(&instance->connections.front());
    assert(instance->n_tcp_connections == 0);

    deinit_all_listeners(instance);

    thread_pool_join();
    thread_pool_deinit();

    lb_hmonitor_deinit();

    pool_commit();

    if (instance->tcp_balancer != nullptr)
        tcp_balancer_free(instance->tcp_balancer);

    if (instance->tcp_stock != nullptr)
        hstock_free(instance->tcp_stock);

    if (instance->balancer != nullptr)
        balancer_free(instance->balancer);

    if (instance->pipe_stock != nullptr)
        pipe_stock_free(instance->pipe_stock);

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
    instance->shutdown_listener.Enable();

    instance->sighup_event.Set(SIGHUP, reload_event_callback, instance);
    instance->sighup_event.Add();
}

void
deinit_signals(struct lb_instance *instance)
{
    instance->shutdown_listener.Disable();
    instance->sighup_event.Delete();
}

int main(int argc, char **argv)
{
    struct lb_instance instance;

    /* configuration */

    parse_cmdline(&instance.cmdline, argc, argv);

    try {
        instance.config = new LbConfig(lb_config_load(instance.pool,
                                                      instance.cmdline.config_path));
    } catch (const std::exception &e) {
        PrintException(e);
        return EXIT_FAILURE;
    }

    if (instance.cmdline.check) {
        int status = EXIT_SUCCESS;

        const ScopeSslGlobalInit ssl_init;

        try {
            lb_check(*instance.config);
        } catch (const std::exception &e) {
            PrintException(e);
            status = EXIT_FAILURE;
        }

        delete instance.config;
        return status;
    }

    /* initialize */

    SetupProcess();

    lb_hmonitor_init(instance.pool);

    const ScopeSslGlobalInit ssl_init;

    direct_global_init();

    fb_pool_init(true);

    init_signals(&instance);

    children_init();

    instance.balancer = balancer_new(*instance.pool);
    instance.tcp_stock = tcp_stock_new(instance.pool,
                                       instance.cmdline.tcp_stock_limit);
    instance.tcp_balancer = tcp_balancer_new(*instance.tcp_stock,
                                             *instance.balancer);

    instance.pipe_stock = pipe_stock_new(instance.pool);

    failure_init();
    bulldog_init(instance.cmdline.bulldog_path);

    try {
        Error error2;

        init_all_controls(&instance);

        if (!init_all_listeners(instance, error2)) {
            deinit_all_controls(&instance);
            fprintf(stderr, "%s\n", error2.GetMessage());
            return EXIT_FAILURE;
        }
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    }

    /* daemonize */

    if (daemonize() < 0)
        exit(2);

    /* launch the access logger */

    if (!log_global_init(instance.cmdline.access_logger))
        return EXIT_FAILURE;

    /* daemonize II */

    if (daemon_user_defined(&instance.cmdline.user))
        capabilities_pre_setuid();

    if (daemon_user_set(&instance.cmdline.user) < 0)
        return EXIT_FAILURE;

    /* can't change to new (empty) rootfs if we may need to reconnect
       to PostgreSQL eventually */
    // TODO: bind-mount the PostgreSQL socket into the new rootfs
    if (!instance.config->HasCertDatabase())
        isolate_from_filesystem();

    if (daemon_user_defined(&instance.cmdline.user))
        capabilities_post_setuid(cap_keep_list, ARRAY_SIZE(cap_keep_list));

#ifdef __linux
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
#endif

    /* main loop */

    if (instance.cmdline.watchdog) {
        /* watchdog */

        all_listeners_event_del(&instance);

        is_watchdog = true;
        instance.launch_worker_event.Init(launch_worker_callback, &instance);
        instance.launch_worker_event.Add(launch_worker_now);
    } else {
        /* this is already the worker process: enable monitors here */
        lb_hmonitor_enable();

        instance.ConnectCertCaches();
    }

    instance.event_base.Dispatch();

    /* cleanup */

    children_shutdown();

    log_global_deinit();

    bulldog_deinit();
    failure_deinit();

    deinit_all_listeners(&instance);
    deinit_all_controls(&instance);

    fb_pool_deinit();

    delete instance.config;

    pool_recycler_clear();

    daemonize_cleanup();
}
