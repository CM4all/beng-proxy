/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_INSTANCE_HXX
#define BENG_PROXY_INSTANCE_HXX

#include "PInstance.hxx"
#include "bp_cmdline.hxx"
#include "bp_config.hxx"
#include "bp_listener.hxx"
#include "bp_connection.hxx"
#include "bp_worker.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "event/TimerEvent.hxx"
#include "spawn/Registry.hxx"
#include "control_handler.hxx"
#include "avahi/Client.hxx"
#include "util/Background.hxx"

#include <boost/intrusive/list.hpp>

#include <forward_list>
#include <memory>

class AccessLogGlue;
class Stock;
class ResourceLoader;
class StockMap;
struct Balancer;
struct TcpBalancer;
struct MemachedStock;
class SpawnService;
class ControlDistribute;
class ControlServer;
struct LocalControl;
class SpawnServerClient;
class TranslateStock;
class LhttpStock;
struct FcgiStock;
struct NfsStock;
struct NfsCache;
class HttpCache;
class FilterCache;

struct BpInstance final : PInstance, ControlHandler {
    BpCmdLine cmdline;
    BpConfig config;

    uint64_t http_request_counter = 0;

    std::forward_list<BPListener> listeners;

    boost::intrusive::list<BpConnection,
                           boost::intrusive::constant_time_size<true>> connections;

    std::unique_ptr<AccessLogGlue> access_log;

    bool should_exit = false;
    ShutdownListener shutdown_listener;
    SignalEvent sighup_event;

    TimerEvent compress_timer;

    /**
     * Registry for jobs running in background, created by the request
     * handler code.
     */
    BackgroundManager background_manager;

    /* child management */
    ChildProcessRegistry child_process_registry;
    SpawnService *spawn_service;
    TimerEvent spawn_worker_event;

    SpawnServerClient *spawn = nullptr;

    boost::intrusive::list<BpWorker,
                           boost::intrusive::constant_time_size<true>> workers;

    /**
     * This object distributes all control packets received by the
     * master process to all worker processes.
     */
    ControlDistribute *control_distribute = nullptr;

    /**
     * The configured control channel servers (see
     * BpConfig::control_listen).  May be empty if none was
     * configured.
     */
    std::forward_list<ControlServer> control_servers;

    /**
     * The implicit per-process control server.  It listens on a local
     * socket "@beng-proxy:PID" and will accept connections only from
     * root or the beng-proxy user.
     */
    LocalControl *local_control_server = nullptr;

    MyAvahiClient avahi_client;

    /* stock */
    TranslateStock *translate_stock = nullptr;
    struct tcache *translate_cache = nullptr;
    Balancer *balancer = nullptr;
    StockMap *tcp_stock = nullptr;
    TcpBalancer *tcp_balancer = nullptr;
    MemachedStock *memcached_stock = nullptr;

    /* cache */
    HttpCache *http_cache = nullptr;

    FilterCache *filter_cache = nullptr;

    LhttpStock *lhttp_stock = nullptr;
    FcgiStock *fcgi_stock = nullptr;

    StockMap *was_stock = nullptr;

    StockMap *delegate_stock = nullptr;

    NfsStock *nfs_stock = nullptr;
    NfsCache *nfs_cache = nullptr;

    Stock *pipe_stock = nullptr;

    ResourceLoader *direct_resource_loader = nullptr;
    ResourceLoader *cached_resource_loader = nullptr;
    ResourceLoader *filter_resource_loader = nullptr;

    /* session */
    TimerEvent session_save_timer;

    BpInstance();
    ~BpInstance();

    void EnableSignals();
    void DisableSignals();

    void ForkCow(bool inherit);

    void Compress();
    void ScheduleCompress();
    void OnCompressTimer();

    void ScheduleSaveSessions();

    /**
     * Transition the current process from "master" to "worker".  Call
     * this after forking in the new worker process.
     */
    void InitWorker();

    pid_t SpawnWorker();
    void ScheduleSpawnWorker();
    void KillAllWorkers();

    /**
     * Handler for #CONTROL_FADE_CHILDREN
     */
    void FadeChildren();

    void ShutdownCallback();

    void ReloadEventCallback(int signo);

    void AddListener(const BpConfig::Listener &c);
    void AddTcpListener(int port);

    void EnableListeners();
    void DisableListeners();

    /* virtual methods from class ControlHandler */
    void OnControlPacket(ControlServer &control_server,
                         enum beng_control_command command,
                         const void *payload, size_t payload_length,
                         SocketAddress address) override;

    void OnControlError(std::exception_ptr ep) override;

private:
    void RespawnWorkerCallback();

    bool AllocatorCompressCallback();

    void SaveSesssions();

    void FreeStocksAndCaches();
};

#endif
