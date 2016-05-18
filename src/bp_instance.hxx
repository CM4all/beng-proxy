/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_INSTANCE_HXX
#define BENG_PROXY_INSTANCE_HXX

#include "RootPool.hxx"
#include "bp_config.hxx"
#include "bp_listener.hxx"
#include "bp_connection.hxx"
#include "bp_worker.hxx"
#include "event/Event.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "event/TimerEvent.hxx"
#include "spawn/Registry.hxx"
#include "control_handler.hxx"

#include <inline/list.h>

#include <boost/intrusive/list.hpp>

#include <forward_list>

// TODO: make this non-optional as soon as the spawner is mature
#define USE_SPAWNER

class Stock;
class ResourceLoader;
struct StockMap;
struct TcpBalancer;
struct MemachedStock;
class SpawnService;
class ControlDistribute;
struct ControlServer;
struct LocalControl;
class SpawnServerClient;
class TranslateStock;
struct LhttpStock;
struct FcgiStock;
struct NfsStock;
struct NfsCache;
class HttpCache;
class FilterCache;

struct BpInstance final : ControlHandler {
    RootPool pool;

    BpConfig config;

    EventLoop event_loop;

    uint64_t http_request_counter = 0;

    std::forward_list<BPListener> listeners;

    boost::intrusive::list<BpConnection,
                           boost::intrusive::constant_time_size<true>> connections;

    bool should_exit = false;
    ShutdownListener shutdown_listener;
    SignalEvent sighup_event;

    /* child management */
    ChildProcessRegistry child_process_registry;
    SpawnService *spawn_service;
    TimerEvent spawn_worker_event;

#ifdef USE_SPAWNER
    SpawnServerClient *spawn = nullptr;
#endif

    boost::intrusive::list<BpWorker,
                           boost::intrusive::constant_time_size<true>> workers;

    /**
     * This object distributes all control packets received by the
     * master process to all worker processes.
     */
    ControlDistribute *control_distribute = nullptr;

    /**
     * The configured control channel server (see --control-listen),
     * nullptr if none was configured.
     */
    ControlServer *control_server = nullptr;

    /**
     * The implicit per-process control server.  It listens on a local
     * socket "@beng-proxy:PID" and will accept connections only from
     * root or the beng-proxy user.
     */
    LocalControl *local_control_server = nullptr;

    /* stock */
    TranslateStock *translate_stock = nullptr;
    struct tcache *translate_cache = nullptr;
    struct balancer *balancer = nullptr;
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

    BpInstance();
    ~BpInstance();

    void ForkCow(bool inherit);

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

    static void ShutdownCallback(void *ctx);
    void ShutdownCallback();

    /* virtual methods from class ControlHandler */
    void OnControlPacket(ControlServer &control_server,
                         enum beng_control_command command,
                         const void *payload, size_t payload_length,
                         SocketAddress address) override;

    void OnControlError(Error &&error) override;

private:
    void RespawnWorkerCallback();
};

struct client_connection;

void
init_signals(BpInstance *instance);

void
deinit_signals(BpInstance *instance);

void
all_listeners_event_add(BpInstance *instance);

void
all_listeners_event_del(BpInstance *instance);

#endif
