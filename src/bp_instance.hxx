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
#include "event/Event.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "event/DelayedTrigger.hxx"
#include "control_handler.hxx"

#include <inline/list.h>

#include <forward_list>

class Stock;
struct StockMap;
struct TcpBalancer;
class ControlDistribute;
struct ControlServer;
struct LocalControl;
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

    EventBase event_base;

    uint64_t http_request_counter = 0;

    std::forward_list<BPListener> listeners;

    struct list_head connections;
    unsigned num_connections = 0;

    bool should_exit = false;
    ShutdownListener shutdown_listener;
    SignalEvent sighup_event;

    /* child management */
    DelayedTrigger respawn_trigger;
    struct list_head workers;
    unsigned num_workers = 0;

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
    struct memcached_stock *memcached_stock = nullptr;

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

    struct resource_loader *resource_loader = nullptr;

    BpInstance();

    void ForkCow(bool inherit);

    /**
     * Handler for #CONTROL_FADE_CHILDREN
     */
    void FadeChildren();

    static void ShutdownCallback(void *ctx);

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
