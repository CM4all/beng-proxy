/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_INSTANCE_HXX
#define BENG_PROXY_LB_INSTANCE_HXX

#include "RootPool.hxx"
#include "lb_cmdline.hxx"
#include "lb_connection.hxx"
#include "lb_hmonitor.hxx"
#include "spawn/ExitListener.hxx"
#include "event/Loop.hxx"
#include "event/TimerEvent.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "spawn/Registry.hxx"

#include <forward_list>
#include <map>

class Stock;
struct StockMap;
struct TcpBalancer;
struct LbConfig;
struct LbCertDatabaseConfig;
struct LbControl;
class lb_listener;
class CertCache;

struct LbInstance final : ExitListener {
    RootPool pool;

    struct lb_cmdline cmdline;

    LbConfig *config;

    EventLoop event_loop;

    uint64_t http_request_counter = 0;

    std::forward_list<LbControl> controls;

    std::forward_list<lb_listener> listeners;

    std::map<std::string, CertCache> cert_dbs;

    LbMonitorMap monitors;

    ChildProcessRegistry child_process_registry;
    TimerEvent launch_worker_event;

    TimerEvent compress_event;

    boost::intrusive::list<LbConnection,
                           boost::intrusive::constant_time_size<true>> connections;

    /**
     * Number of #lb_tcp instances.
     */
    unsigned n_tcp_connections = 0;

    bool should_exit = false;
    ShutdownListener shutdown_listener;
    SignalEvent sighup_event;

    /* stock */
    struct balancer *balancer;
    StockMap *tcp_stock;
    TcpBalancer *tcp_balancer;

    Stock *pipe_stock;

    LbInstance();
    ~LbInstance();

    /**
     * Transition the current process from "master" to "worker".  Call
     * this after forking in the new worker process.
     */
    void InitWorker();

    /**
     * Compress memory allocators, try to return unused memory areas
     * to the kernel.
     */
    void Compress();

    CertCache &GetCertCache(const LbCertDatabaseConfig &cert_db_config);
    void ConnectCertCaches();
    void DisconnectCertCaches();

    unsigned FlushSSLSessionCache(long tm);

    void ShutdownCallback();

    void ReloadEventCallback(int signo);

    /* virtual methods from class ExitListener */
    void OnChildProcessExit(int status) override;

private:
    void LaunchWorker();
    void OnCompressTimer();
};

struct client_connection;

void
init_signals(LbInstance *instance);

void
deinit_signals(LbInstance *instance);

#endif
