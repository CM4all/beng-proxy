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
#include "ssl/ssl_init.hxx"
#include "pool.hxx"
#include "thread_pool.hxx"
#include "fb_pool.hxx"
#include "capabilities.hxx"
#include "isolate.hxx"
#include "event/Callback.hxx"
#include "system/SetupProcess.hxx"
#include "util/PrintException.hxx"
#include "util/Macros.hxx"

#include <daemon/log.h>

#include <systemd/sd-daemon.h>
#include <postgresql/libpq-fe.h>

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

void
LbInstance::OnChildProcessExit(gcc_unused int status)
{
    worker_pid = 0;

    if (!should_exit)
        launch_worker_event.Add(launch_worker_delayed);
}

void
LbInstance::LaunchWorker()
{
    assert(is_watchdog);
    assert(worker_pid <= 0);

    worker_pid = fork();
    if (worker_pid < 0) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));

        launch_worker_event.Add(launch_worker_delayed);
        return;
    }

    if (worker_pid == 0) {
        is_watchdog = false;

        event_loop.Reinit();

        child_process_registry.Clear();
        all_listeners_event_add(this);

        enable_all_controls(this);

        InitWorker();
        return;
    }

    child_process_registry.Add(worker_pid, "worker", this);
}

void
LbInstance::ShutdownCallback()
{
    if (should_exit)
        return;

    should_exit = true;
    deinit_signals(this);
    thread_pool_stop();

    if (is_watchdog && worker_pid > 0)
        kill(worker_pid, SIGTERM);

    avahi_client.Close();

    child_process_registry.SetVolatile();

    if (is_watchdog)
        launch_worker_event.Cancel();
    else {
        compress_event.Cancel();
    }

    deinit_all_controls(this);

    if (!is_watchdog)
        DisconnectCertCaches();

    while (!connections.empty())
        lb_connection_close(&connections.front());
    assert(n_tcp_connections == 0);

    deinit_all_listeners(this);

    thread_pool_join();

    monitors.Clear();

    pool_commit();

    if (tcp_balancer != nullptr)
        tcp_balancer_free(tcp_balancer);

    if (tcp_stock != nullptr)
        delete tcp_stock;

    if (balancer != nullptr)
        balancer_free(balancer);

    if (pipe_stock != nullptr)
        pipe_stock_free(pipe_stock);

    pool_commit();
}

void
LbInstance::ReloadEventCallback(int)
{
    unsigned n_ssl_sessions = FlushSSLSessionCache(LONG_MAX);
    daemon_log(3, "flushed %u SSL sessions\n", n_ssl_sessions);

    Compress();
}

void
init_signals(LbInstance *instance)
{
    instance->shutdown_listener.Enable();
    instance->sighup_event.Add();
}

void
deinit_signals(LbInstance *instance)
{
    instance->shutdown_listener.Disable();
    instance->sighup_event.Delete();
}

int main(int argc, char **argv)
{
    const ScopeFbPoolInit fb_pool_init;
    LbInstance instance;

    /* configuration */

    LbConfig config;
    ParseCommandLine(instance.cmdline, config, argc, argv);

    try {
        LoadConfigFile(*instance.pool, config,
                       instance.cmdline.config_path);
    } catch (const std::exception &e) {
        PrintException(e);
        return EXIT_FAILURE;
    }

    instance.config = &config;

    if (instance.cmdline.check) {
        int status = EXIT_SUCCESS;

        const ScopeSslGlobalInit ssl_init;

        try {
            lb_check(instance.event_loop, *instance.config);
        } catch (const std::exception &e) {
            PrintException(e);
            status = EXIT_FAILURE;
        }

        delete instance.config;
        return status;
    }

    /* initialize */

    SetupProcess();

    const ScopeSslGlobalInit ssl_init;

    /* prevent libpq from initializing libssl & libcrypto again */
    PQinitOpenSSL(0, 0);

    direct_global_init();

    init_signals(&instance);

    try {
        init_all_controls(&instance);
        init_all_listeners(instance);
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    }

    instance.balancer = balancer_new(*instance.pool, instance.event_loop);
    instance.tcp_stock = tcp_stock_new(instance.event_loop,
                                       instance.cmdline.tcp_stock_limit);
    instance.tcp_balancer = tcp_balancer_new(*instance.tcp_stock,
                                             *instance.balancer);

    instance.pipe_stock = pipe_stock_new(instance.event_loop);

    failure_init();
    bulldog_init(instance.cmdline.bulldog_path);

    /* launch the access logger */

    if (!log_global_init(config.access_logger.c_str(),
                         &instance.cmdline.logger_user))
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
        isolate_from_filesystem(instance.config->HasZeroConf());

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
        instance.launch_worker_event.Add(launch_worker_now);
    } else {
        /* this is already the worker process: enable monitors here */
        instance.InitWorker();
    }

    /* tell systemd we're ready */
    sd_notify(0, "READY=1");

    instance.event_loop.Dispatch();

    /* cleanup */

    log_global_deinit();

    bulldog_deinit();
    failure_deinit();

    deinit_all_listeners(&instance);
    deinit_all_controls(&instance);

    thread_pool_deinit();

    pool_recycler_clear();
}
