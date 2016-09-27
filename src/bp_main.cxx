/*
 * The main source of the Beng proxy server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "direct.hxx"
#include "bp_cmdline.hxx"
#include "bp_instance.hxx"
#include "bp_connection.hxx"
#include "bp_worker.hxx"
#include "bp_global.hxx"
#include "crash.hxx"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "session_manager.hxx"
#include "session_save.hxx"
#include "tstock.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "memcached/memcached_stock.hxx"
#include "stock/MapStock.hxx"
#include "tcache.hxx"
#include "http_cache.hxx"
#include "lhttp_stock.hxx"
#include "fcgi/Stock.hxx"
#include "was/was_stock.hxx"
#include "delegate/Stock.hxx"
#include "fcache.hxx"
#include "thread_pool.hxx"
#include "stopwatch.hxx"
#include "failure.hxx"
#include "bulldog.h"
#include "balancer.hxx"
#include "pipe_stock.hxx"
#include "DirectResourceLoader.hxx"
#include "CachedResourceLoader.hxx"
#include "FilterResourceLoader.hxx"
#include "bp_control.hxx"
#include "log-glue.h"
#include "ua_classification.hxx"
#include "ssl/ssl_init.hxx"
#include "ssl/ssl_client.hxx"
#include "system/SetupProcess.hxx"
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
#include "util/Error.hxx"
#include "util/Macros.hxx"
#include "util/PrintException.hxx"

#include <daemon/log.h>

#include <systemd/sd-daemon.h>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef HAVE_LIBNFS
#include "nfs_stock.hxx"
#include "nfs_cache.hxx"
#endif

#ifndef NDEBUG
bool debug_mode = false;
#endif

static constexpr cap_value_t cap_keep_list[] = {
#ifndef USE_SPAWNER
    /* keep the KILL capability to be able to kill child processes
       that have switched to another uid (e.g. via JailCGI) */
    CAP_KILL,
#endif

#ifdef HAVE_LIBNFS
    /* allow libnfs to bind to privileged ports, which in turn allows
       disabling the "insecure" flag on the NFS server */
    CAP_NET_BIND_SERVICE,
#endif
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

#ifdef USE_SPAWNER
    spawn->Shutdown();
#endif

    listeners.clear();

    connections.clear_and_dispose(BpConnection::Disposer());

    pool_commit();

    avahi_client.Close();

    spawn_worker_event.Cancel();

    child_process_registry.SetVolatile();

    thread_pool_join();

    KillAllWorkers();

    background_manager.AbortAll();

    session_save_timer.Cancel();
    session_save_deinit();
    session_manager_deinit();

    if (translate_cache != nullptr)
        translate_cache_close(translate_cache);

    if (translate_stock != nullptr)
        tstock_free(translate_stock);

    if (http_cache != nullptr) {
        http_cache_close(http_cache);
        http_cache = nullptr;
    }

    if (filter_cache != nullptr) {
        filter_cache_close(filter_cache);
        filter_cache = nullptr;
    }

    if (lhttp_stock != nullptr) {
        lhttp_stock_free(lhttp_stock);
        lhttp_stock = nullptr;
    }

    if (fcgi_stock != nullptr) {
        fcgi_stock_free(fcgi_stock);
        fcgi_stock = nullptr;
    }

    if (was_stock != nullptr) {
        delete was_stock;
        was_stock = nullptr;
    }

    if (memcached_stock != nullptr)
        memcached_stock_free(memcached_stock);

    if (tcp_balancer != nullptr)
        tcp_balancer_free(tcp_balancer);

    if (tcp_stock != nullptr)
        delete tcp_stock;

    if (balancer != nullptr)
        balancer_free(balancer);

    if (delegate_stock != nullptr)
        delete delegate_stock;

#ifdef HAVE_LIBNFS
    if (nfs_cache != nullptr)
        nfs_cache_free(nfs_cache);

    if (nfs_stock != nullptr)
        nfs_stock_free(nfs_stock);
#endif

    if (pipe_stock != nullptr)
        pipe_stock_free(pipe_stock);

    local_control_handler_deinit(this);
    global_control_handler_deinit(this);

    fb_pool_disable();

    pool_commit();
}

void
BpInstance::ReloadEventCallback(int)
{
    daemon_log(3, "caught SIGHUP, flushing all caches (pid=%d)\n",
               (int)getpid());

    translate_cache_flush(*translate_cache);
    http_cache_flush(*http_cache);
    if (filter_cache != nullptr)
        filter_cache_flush(*filter_cache);
    fb_pool_compress();
}

void
BpInstance::EnableSignals()
{
    shutdown_listener.Enable();
    sighup_event.Add();
}

void
BpInstance::DisableSignals()
{
    shutdown_listener.Disable();
    sighup_event.Delete();
}

void
BpInstance::AddListener(const BpConfig::Listener &c)
{
    Error error;

    listeners.emplace_front(*this, c.tag.empty() ? nullptr : c.tag.c_str());
    auto &listener = listeners.front();

    if (!listener.Listen(c.address.GetFamily(), SOCK_STREAM, 0,
                         c.address, c.reuse_port,
                         c.interface.empty() ? nullptr : c.interface.c_str(),
                         error)) {
        fprintf(stderr, "%s\n", error.GetMessage());
        exit(2);
    }

    listener.SetTcpDeferAccept(10);

    if (!c.zeroconf_type.empty()) {
        /* ask the kernel for the effective address via getsockname(),
           because it may have changed, e.g. if the kernel has
           selected a port for us */
        const auto local_address = listener.GetLocalAddress();
        if (local_address.IsDefined())
            avahi_client.AddService(c.zeroconf_type.c_str(),
                                    local_address);
    }
}

void
BpInstance::AddTcpListener(int port)
{
    Error error;

    listeners.emplace_front(*this, nullptr);
    auto &listener = listeners.front();
    if (!listener.ListenTCP(port, error)) {
        fprintf(stderr, "%s\n", error.GetMessage());
        exit(2);
    }

    listener.SetTcpDeferAccept(10);
}

int main(int argc, char **argv)
try {
#ifndef NDEBUG
    if (geteuid() != 0)
        debug_mode = true;
#endif

    BpInstance instance;

    /* configuration */

    parse_cmdline(instance.cmdline, instance.config, argc, argv);

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

    fb_pool_init(instance.event_loop, true);

#ifdef USE_SPAWNER
    /* note: this function call passes a temporary SpawnConfig copy,
       because the reference will be evaluated in the child process
       after ~BpInstance() has been called */
    instance.spawn = StartSpawnServer(SpawnConfig(instance.config.spawn),
                                      instance.child_process_registry,
                                      [argc, argv, &instance](){
            /* rename the process */
            size_t name_size = strlen(argv[0]);
            for (int i = 0; i < argc; ++i)
                memset(argv[i], 0, strlen(argv[i]));
            strncpy(argv[0], "spawn", name_size);

            instance.event_loop.Reinit();

            global_control_handler_deinit(&instance);
            instance.listeners.clear();
            instance.DisableSignals();

            instance.~BpInstance();
        });
    instance.spawn_service = instance.spawn;
#else
    LocalSpawnService spawn_service(instance.config.spawn,
                                    instance.child_process_registry);
    instance.spawn_service = &spawn_service;
#endif

    if (!crash_global_init()) {
        fprintf(stderr, "crash_global_init() failed\n");
        return EXIT_FAILURE;
    }

    session_manager_init(instance.event_loop,
                         instance.config.session_idle_timeout,
                         instance.config.cluster_size,
                         instance.config.cluster_node);

    if (instance.config.session_save_path != nullptr) {
        session_save_init(instance.config.session_save_path);
        instance.ScheduleSaveSessions();
    }

    local_control_handler_init(&instance);

    try {
        local_control_handler_open(&instance);
    } catch (const std::exception &e) {
        PrintException(e);
    }

    instance.balancer = balancer_new(*instance.pool, instance.event_loop);
    instance.tcp_stock = tcp_stock_new(instance.event_loop,
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

        instance.translate_cache = translate_cache_new(*instance.pool,
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

#ifdef HAVE_LIBNFS
    instance.nfs_stock = nfs_stock_new(instance.event_loop, instance.pool);
    instance.nfs_cache = nfs_cache_new(*instance.pool,
                                       instance.config.nfs_cache_size,
                                       *instance.nfs_stock,
                                       instance.event_loop);
#endif

    instance.direct_resource_loader =
        new DirectResourceLoader(instance.event_loop,
                                 instance.tcp_balancer,
                                 *instance.spawn_service,
                                 instance.lhttp_stock,
                                 instance.fcgi_stock,
                                 instance.was_stock,
                                 instance.delegate_stock,
                                 instance.nfs_cache);

    instance.http_cache = http_cache_new(*instance.pool,
                                         instance.config.http_cache_size,
                                         instance.memcached_stock,
                                         instance.event_loop,
                                         *instance.direct_resource_loader);

    instance.cached_resource_loader =
        new CachedResourceLoader(*instance.http_cache);

    instance.pipe_stock = pipe_stock_new(instance.event_loop);

    if (instance.config.filter_cache_size > 0) {
        instance.filter_cache = filter_cache_new(instance.pool,
                                                 instance.config.filter_cache_size,
                                                 instance.event_loop,
                                                 *instance.direct_resource_loader);
        instance.filter_resource_loader =
            new FilterResourceLoader(*instance.filter_cache);
    } else
        instance.filter_resource_loader = instance.direct_resource_loader;

    failure_init();
    bulldog_init(instance.config.bulldog_path);

    global_translate_cache = instance.translate_cache;
    global_pipe_stock = instance.pipe_stock;

    /* launch the access logger */

    if (!log_global_init(instance.cmdline.access_logger,
                         &instance.cmdline.logger_user))
        return EXIT_FAILURE;

    /* daemonize II */

    if (daemon_user_defined(&instance.cmdline.user))
        capabilities_pre_setuid();

    if (daemon_user_set(&instance.cmdline.user) < 0)
        return EXIT_FAILURE;

    if (daemon_user_defined(&instance.cmdline.user))
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

    log_global_deinit();

    bulldog_deinit();
    failure_deinit();

#ifdef USE_SPAWNER
    delete instance.spawn;
#endif

    thread_pool_deinit();

    fb_pool_deinit();

    ssl_client_deinit();

    crash_global_deinit();

    ua_classification_deinit();
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
}
