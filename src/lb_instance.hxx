/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_INSTANCE_HXX
#define BENG_PROXY_LB_INSTANCE_HXX

#include "PInstance.hxx"
#include "lb_cmdline.hxx"
#include "lb_cluster.hxx"
#include "lb_connection.hxx"
#include "lb_hmonitor.hxx"
#include "lb/LuaHandler.hxx"
#include "lb/HttpConnection.hxx"
#include "event/TimerEvent.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "avahi/Client.hxx"

#include <forward_list>
#include <map>

class Stock;
class StockMap;
struct Balancer;
struct TcpBalancer;
struct LbConfig;
struct LbCertDatabaseConfig;
struct LbControl;
class LbListener;
class CertCache;

struct LbInstance final : PInstance {
    LbCmdLine cmdline;

    LbConfig *config;

    uint64_t http_request_counter = 0;

    std::forward_list<LbControl> controls;

    /**
     * A map of clusters which need run-time data.
     */
    LbClusterMap clusters;

    /**
     * A map of configured #LbLuaHandler instances.
     */
    LbLuaHandlerMap lua_handlers;

    std::forward_list<LbListener> listeners;

    std::map<std::string, CertCache> cert_dbs;

    LbMonitorMap monitors;

    MyAvahiClient avahi_client;

    TimerEvent compress_event;

    boost::intrusive::list<LbHttpConnection,
                           boost::intrusive::constant_time_size<true>> http_connections;

    boost::intrusive::list<LbConnection,
                           boost::intrusive::constant_time_size<true>> tcp_connections;

    bool should_exit = false;
    ShutdownListener shutdown_listener;
    SignalEvent sighup_event;

    /* stock */
    Balancer *balancer;
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

private:
    void OnCompressTimer();
};

struct client_connection;

void
init_signals(LbInstance *instance);

void
deinit_signals(LbInstance *instance);

#endif
