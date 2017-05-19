/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "direct.hxx"
#include "lb_instance.hxx"
#include "lb_check.hxx"
#include "lb_setup.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "stock/MapStock.hxx"
#include "failure.hxx"
#include "bulldog.hxx"
#include "balancer.hxx"
#include "pipe_stock.hxx"
#include "log_glue.hxx"
#include "lb_config.hxx"
#include "ssl/ssl_init.hxx"
#include "pool.hxx"
#include "thread_pool.hxx"
#include "fb_pool.hxx"
#include "capabilities.hxx"
#include "system/Isolate.hxx"
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

void
LbInstance::ShutdownCallback()
{
    if (should_exit)
        return;

    should_exit = true;
    deinit_signals(this);
    thread_pool_stop();

    avahi_client.Close();

    compress_event.Cancel();

    deinit_all_controls(this);

    translation_handlers.Clear();

    DisconnectCertCaches();

    while (!tcp_connections.empty())
        tcp_connections.front().Destroy();

    while (!http_connections.empty())
        http_connections.front().CloseAndDestroy();

    deinit_all_listeners(this);

    thread_pool_join();

    monitors.Clear();

    pool_commit();

    if (tcp_balancer != nullptr)
        tcp_balancer_free(tcp_balancer);

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
    instance->sighup_event.Enable();
}

void
deinit_signals(LbInstance *instance)
{
    instance->shutdown_listener.Disable();
    instance->sighup_event.Disable();
}

int main(int argc, char **argv)
try {
    const ScopeFbPoolInit fb_pool_init;
    LbInstance instance;

    /* configuration */

    LbConfig config;
    ParseCommandLine(instance.cmdline, config, argc, argv);

    LoadConfigFile(config, instance.cmdline.config_path);

    instance.config = &config;

    if (instance.cmdline.check) {
        const ScopeSslGlobalInit ssl_init;
        lb_check(instance.event_loop, *instance.config);
        return EXIT_SUCCESS;
    }

    /* initialize */

    SetupProcess();

    const ScopeSslGlobalInit ssl_init;

    /* prevent libpq from initializing libssl & libcrypto again */
    PQinitOpenSSL(0, 0);

    direct_global_init();

    init_signals(&instance);

    init_all_controls(&instance);
    init_all_listeners(instance);

    instance.balancer = balancer_new(instance.event_loop);
    instance.tcp_stock = tcp_stock_new(instance.event_loop,
                                       instance.cmdline.tcp_stock_limit);
    instance.tcp_balancer = tcp_balancer_new(*instance.tcp_stock,
                                             *instance.balancer);

    instance.pipe_stock = pipe_stock_new(instance.event_loop);

    failure_init();
    bulldog_init(instance.cmdline.bulldog_path);

    /* launch the access logger */

    log_global_init(config.access_logger.c_str(),
                    &instance.cmdline.logger_user);

    /* daemonize II */

    if (!instance.cmdline.user.IsEmpty())
        capabilities_pre_setuid();

    instance.cmdline.user.Apply();

    /* can't change to new (empty) rootfs if we may need to reconnect
       to PostgreSQL eventually */
    // TODO: bind-mount the PostgreSQL socket into the new rootfs
    if (!instance.config->HasCertDatabase())
        isolate_from_filesystem(instance.config->HasZeroConf());

    if (!instance.cmdline.user.IsEmpty())
        capabilities_post_setuid(cap_keep_list, ARRAY_SIZE(cap_keep_list));

#ifdef __linux
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
#endif

    /* main loop */

    instance.InitWorker();

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
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
