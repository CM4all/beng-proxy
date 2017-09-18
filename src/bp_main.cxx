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
#include "bp_cmdline.hxx"
#include "bp_instance.hxx"
#include "bp/Connection.hxx"
#include "bp/Worker.hxx"
#include "bp_global.hxx"
#include "crash.hxx"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "session_manager.hxx"
#include "session_save.hxx"
#include "tcp_stock.hxx"
#include "translation/Stock.hxx"
#include "translation/Cache.hxx"
#include "tcp_balancer.hxx"
#include "memcached/memcached_stock.hxx"
#include "stock/MapStock.hxx"
#include "http_cache.hxx"
#include "lhttp_stock.hxx"
#include "fcgi/Stock.hxx"
#include "was/Stock.hxx"
#include "delegate/Stock.hxx"
#include "fcache.hxx"
#include "thread_pool.hxx"
#include "stopwatch.hxx"
#include "failure.hxx"
#include "bulldog.hxx"
#include "balancer.hxx"
#include "pipe_stock.hxx"
#include "nfs/Stock.hxx"
#include "nfs/Cache.hxx"
#include "DirectResourceLoader.hxx"
#include "CachedResourceLoader.hxx"
#include "FilterResourceLoader.hxx"
#include "bp_control.hxx"
#include "access_log/Glue.hxx"
#include "ua_classification.hxx"
#include "ssl/ssl_init.hxx"
#include "ssl/ssl_client.hxx"
#include "system/SetupProcess.hxx"
#include "system/ProcessName.hxx"
#include "system/Error.hxx"
#include "capabilities.hxx"
#include "spawn/Local.hxx"
#include "spawn/Glue.hxx"
#include "spawn/Client.hxx"
#include "event/Duration.hxx"
#include "address_list.hxx"
#include "net/SocketAddress.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/ServerSocket.hxx"
#include "io/Logger.hxx"
#include "util/Macros.hxx"
#include "util/PrintException.hxx"

#include <systemd/sd-daemon.h>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef NDEBUG
bool debug_mode = false;
#endif

static constexpr cap_value_t cap_keep_list[] = {
    /* allow libnfs to bind to privileged ports, which in turn allows
       disabling the "insecure" flag on the NFS server */
    CAP_NET_BIND_SERVICE,
};

void
BpInstance::EnableListeners()
{
    for (auto &listener : listeners)
        listener.AddEvent();
}

void
BpInstance::DisableListeners()
{
    for (auto &listener : listeners)
        listener.RemoveEvent();
}

void
BpInstance::ShutdownCallback()
{
    if (should_exit)
        return;

    should_exit = true;
    DisableSignals();
    thread_pool_stop();

    spawn->Shutdown();

    listeners.clear();

    connections.clear_and_dispose(BpConnection::Disposer());

    pool_commit();

    avahi_client.Close();

    compress_timer.Cancel();

    spawn_worker_event.Cancel();

    child_process_registry.SetVolatile();

    thread_pool_join();

    KillAllWorkers();

    background_manager.AbortAll();

    session_save_timer.Cancel();
    session_save_deinit();
    session_manager_deinit();

    FreeStocksAndCaches();

    local_control_handler_deinit(this);
    global_control_handler_deinit(this);

    pool_commit();
}

void
BpInstance::ReloadEventCallback(int)
{
    LogConcat(3, "main", "caught SIGHUP, flushing all caches (pid=",
              (int)getpid(), ")");

    translate_cache_flush(*translate_cache);
    http_cache_flush(*http_cache);
    if (filter_cache != nullptr)
        filter_cache_flush(*filter_cache);
    Compress();
}

void
BpInstance::EnableSignals()
{
    shutdown_listener.Enable();
    sighup_event.Enable();
}

void
BpInstance::DisableSignals()
{
    shutdown_listener.Disable();
    sighup_event.Disable();
}

void
BpInstance::AddListener(const BpConfig::Listener &c)
{
    listeners.emplace_front(*this, c.tag.empty() ? nullptr : c.tag.c_str());
    auto &listener = listeners.front();

    const char *const interface = c.GetInterface();

    listener.Listen(c.bind_address, c.reuse_port, c.free_bind, interface);

    listener.SetTcpDeferAccept(10);

    if (!c.zeroconf_service.empty()) {
        /* ask the kernel for the effective address via getsockname(),
           because it may have changed, e.g. if the kernel has
           selected a port for us */
        const auto local_address = listener.GetLocalAddress();
        if (local_address.IsDefined())
            avahi_client.AddService(c.zeroconf_service.c_str(),
                                    interface, local_address);
    }
}

void
BpInstance::AddTcpListener(int port)
{
    listeners.emplace_front(*this, nullptr);
    auto &listener = listeners.front();
    listener.ListenTCP(port);
    listener.SetTcpDeferAccept(10);
}

int main(int argc, char **argv)
try {
    InitProcessName(argc, argv);

#ifndef NDEBUG
    if (geteuid() != 0)
        debug_mode = true;
#endif

    const ScopeFbPoolInit fb_pool_init;

    BpInstance instance;

    /* configuration */

    ParseCommandLine(instance.cmdline, instance.config, argc, argv);

    if (instance.cmdline.config_file != nullptr)
        LoadConfigFile(instance.config, instance.cmdline.config_file);

    if (instance.config.ports.empty() && instance.config.listen.empty())
        instance.config.ports.push_back(debug_mode ? 8080 : 80);

    /* initialize */

    if (instance.config.stopwatch)
        stopwatch_enable();

    SetupProcess();

    const ScopeSslGlobalInit ssl_init;
    ssl_client_init();

    direct_global_init();

    instance.EnableSignals();

    for (auto i : instance.config.ports)
        instance.AddTcpListener(i);

    for (const auto &i : instance.config.listen)
        instance.AddListener(i);

    global_control_handler_init(&instance);

    if (instance.config.num_workers == 1)
        /* in single-worker mode with watchdog master process, let
           only the one worker handle control commands */
        global_control_handler_disable(instance);

    /* note: this function call passes a temporary SpawnConfig copy,
       because the reference will be evaluated in the child process
       after ~BpInstance() has been called */
    instance.spawn = StartSpawnServer(SpawnConfig(instance.config.spawn),
                                      instance.child_process_registry,
                                      nullptr,
                                      [&instance](){
            instance.event_loop.Reinit();

            global_control_handler_deinit(&instance);
            instance.listeners.clear();
            instance.DisableSignals();

            instance.~BpInstance();
        });
    instance.spawn_service = instance.spawn;

    if (!crash_global_init()) {
        fprintf(stderr, "crash_global_init() failed\n");
        return EXIT_FAILURE;
    }

    session_manager_init(instance.event_loop,
                         instance.config.session_idle_timeout,
                         instance.config.cluster_size,
                         instance.config.cluster_node);

    if (!instance.config.session_save_path.empty()) {
        session_save_init(instance.config.session_save_path.c_str());
        instance.ScheduleSaveSessions();
    }

    local_control_handler_init(&instance);

    try {
        local_control_handler_open(&instance);
    } catch (const std::exception &e) {
        PrintException(e);
    }

    instance.balancer = new Balancer(instance.event_loop);
    instance.tcp_stock = new TcpStock(instance.event_loop,
                                      instance.config.tcp_stock_limit);
    instance.tcp_balancer = tcp_balancer_new(*instance.tcp_stock,
                                             *instance.balancer);

    const AddressList memcached_server(ShallowCopy(),
                                       instance.config.memcached_server);
    if (!instance.config.memcached_server.empty())
        instance.memcached_stock =
            memcached_stock_new(instance.event_loop,
                                *instance.tcp_balancer,
                                memcached_server);

    if (instance.config.translation_socket != nullptr) {
        instance.translate_stock =
            tstock_new(instance.event_loop,
                       instance.config.translation_socket,
                       instance.config.translate_stock_limit);

        instance.translate_cache = translate_cache_new(instance.root_pool,
                                                       instance.event_loop,
                                                       *instance.translate_stock,
                                                       instance.config.translate_cache_size,
                                                       false);
    }

    instance.lhttp_stock = lhttp_stock_new(0, 16, instance.event_loop,
                                           *instance.spawn_service);

    instance.fcgi_stock = fcgi_stock_new(instance.config.fcgi_stock_limit,
                                         instance.config.fcgi_stock_max_idle,
                                         instance.event_loop,
                                         *instance.spawn_service);

    instance.was_stock = was_stock_new(instance.config.was_stock_limit,
                                       instance.config.was_stock_max_idle,
                                       instance.event_loop,
                                       *instance.spawn_service);

    instance.delegate_stock = delegate_stock_new(instance.event_loop,
                                                 *instance.spawn_service);

    instance.nfs_stock = nfs_stock_new(instance.event_loop,
                                       instance.root_pool);
    instance.nfs_cache = nfs_cache_new(instance.root_pool,
                                       instance.config.nfs_cache_size,
                                       *instance.nfs_stock,
                                       instance.event_loop);

    instance.direct_resource_loader =
        new DirectResourceLoader(instance.event_loop,
                                 instance.tcp_balancer,
                                 *instance.spawn_service,
                                 instance.lhttp_stock,
                                 instance.fcgi_stock,
                                 instance.was_stock,
                                 instance.delegate_stock,
                                 instance.nfs_cache);

    instance.http_cache = http_cache_new(instance.root_pool,
                                         instance.config.http_cache_size,
                                         instance.memcached_stock,
                                         instance.event_loop,
                                         *instance.direct_resource_loader);

    instance.cached_resource_loader =
        new CachedResourceLoader(*instance.http_cache);

    instance.pipe_stock = pipe_stock_new(instance.event_loop);

    if (instance.config.filter_cache_size > 0) {
        instance.filter_cache = filter_cache_new(instance.root_pool,
                                                 instance.config.filter_cache_size,
                                                 instance.event_loop,
                                                 *instance.direct_resource_loader);
        instance.filter_resource_loader =
            new FilterResourceLoader(*instance.filter_cache);
    } else
        instance.filter_resource_loader = instance.direct_resource_loader;

    const ScopeFailureInit failure;
    bulldog_init(instance.config.bulldog_path);

    global_translate_cache = instance.translate_cache;
    global_pipe_stock = instance.pipe_stock;

    /* launch the access logger */

    instance.access_log.reset(AccessLogGlue::Create(instance.config.access_log,
                                                    &instance.cmdline.logger_user));

    /* daemonize II */

    if (!instance.cmdline.user.IsEmpty())
        capabilities_pre_setuid();

    instance.cmdline.user.Apply();

    if (!instance.cmdline.user.IsEmpty())
        capabilities_post_setuid(cap_keep_list, ARRAY_SIZE(cap_keep_list));

    /* create worker processes */

    if (instance.config.num_workers > 0) {
        /* the master process shouldn't work */
        instance.DisableListeners();

        /* spawn the first worker really soon */
        instance.spawn_worker_event.Add(EventDuration<0, 10000>::value);
    } else {
        instance.InitWorker();
    }

    /* tell systemd we're ready */
    sd_notify(0, "READY=1");

    /* main loop */

    instance.event_loop.Dispatch();

    /* cleanup */

    bulldog_deinit();

    delete instance.spawn;

    thread_pool_deinit();

    ssl_client_deinit();

    crash_global_deinit();

    ua_classification_deinit();
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
