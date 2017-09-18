/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "direct.hxx"
#include "lb_cmdline.hxx"
#include "lb/Instance.hxx"
#include "lb_check.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "failure.hxx"
#include "bulldog.hxx"
#include "balancer.hxx"
#include "pipe_stock.hxx"
#include "access_log/Glue.hxx"
#include "lb/Config.hxx"
#include "ssl/ssl_init.hxx"
#include "pool.hxx"
#include "thread_pool.hxx"
#include "fb_pool.hxx"
#include "capabilities.hxx"
#include "system/Isolate.hxx"
#include "system/SetupProcess.hxx"
#include "util/PrintException.hxx"
#include "util/Macros.hxx"

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

    DeinitAllControls();

    while (!tcp_connections.empty())
        tcp_connections.front().Destroy();

    while (!http_connections.empty())
        http_connections.front().CloseAndDestroy();

    goto_map.Clear();

    DisconnectCertCaches();

    DeinitAllListeners();

    thread_pool_join();

    monitors.Clear();

    pool_commit();

    if (tcp_balancer != nullptr)
        tcp_balancer_free(tcp_balancer);

    delete tcp_stock;
    delete std::exchange(balancer, nullptr);

    if (pipe_stock != nullptr)
        pipe_stock_free(pipe_stock);

    pool_commit();
}

void
LbInstance::ReloadEventCallback(int)
{
    unsigned n_ssl_sessions = FlushSSLSessionCache(LONG_MAX);
    logger(3, "flushed ", n_ssl_sessions, " SSL sessions");

    goto_map.FlushCaches();

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

    /* configuration */

    LbCmdLine cmdline;
    LbConfig config;
    ParseCommandLine(cmdline, config, argc, argv);

    LoadConfigFile(config, cmdline.config_path);

    LbInstance instance(config);

    if (cmdline.check) {
        const ScopeSslGlobalInit ssl_init;
        lb_check(instance.event_loop, config);
        return EXIT_SUCCESS;
    }

    /* initialize */

    SetupProcess();

    const ScopeSslGlobalInit ssl_init;

    /* prevent libpq from initializing libssl & libcrypto again */
    PQinitOpenSSL(0, 0);

    direct_global_init();

    init_signals(&instance);

    instance.InitAllControls();
    instance.InitAllListeners();

    instance.balancer = new Balancer(instance.event_loop);
    instance.tcp_stock = new TcpStock(instance.event_loop,
                                      cmdline.tcp_stock_limit);
    instance.tcp_balancer = tcp_balancer_new(*instance.tcp_stock,
                                             *instance.balancer);

    instance.pipe_stock = pipe_stock_new(instance.event_loop);

    const ScopeFailureInit failure;
    bulldog_init(cmdline.bulldog_path);

    /* launch the access logger */

    instance.access_log.reset(AccessLogGlue::Create(config.access_log,
                                                    &cmdline.logger_user));

    /* daemonize II */

    if (!cmdline.user.IsEmpty())
        capabilities_pre_setuid();

    cmdline.user.Apply();

    /* can't change to new (empty) rootfs if we may need to reconnect
       to PostgreSQL eventually */
    // TODO: bind-mount the PostgreSQL socket into the new rootfs
    if (!config.HasCertDatabase())
        isolate_from_filesystem(config.HasZeroConf());

    if (!cmdline.user.IsEmpty())
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

    bulldog_deinit();

    instance.DeinitAllListeners();
    instance.DeinitAllControls();

    thread_pool_deinit();
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
