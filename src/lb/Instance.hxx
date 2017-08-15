/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_INSTANCE_HXX
#define BENG_PROXY_LB_INSTANCE_HXX

#include "PInstance.hxx"
#include "GotoMap.hxx"
#include "MonitorMap.hxx"
#include "HttpConnection.hxx"
#include "TcpConnection.hxx"
#include "event/TimerEvent.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "avahi/Client.hxx"
#include "io/Logger.hxx"

#include <forward_list>
#include <memory>
#include <map>

class AccessLogGlue;
class Stock;
class StockMap;
struct Balancer;
struct TcpBalancer;
struct LbConfig;
struct LbCertDatabaseConfig;
class LbControl;
class LbListener;
class CertCache;

struct LbInstance final : PInstance {
    const LbConfig &config;

    const Logger logger;

    uint64_t http_request_counter = 0;

    std::forward_list<LbControl> controls;

    MyAvahiClient avahi_client;

    LbGotoMap goto_map;

    std::forward_list<LbListener> listeners;

    std::map<std::string, CertCache> cert_dbs;

    LbMonitorMap monitors;

    TimerEvent compress_event;

    boost::intrusive::list<LbHttpConnection,
                           boost::intrusive::constant_time_size<true>> http_connections;

    boost::intrusive::list<LbTcpConnection,
                           boost::intrusive::constant_time_size<true>> tcp_connections;

    std::unique_ptr<AccessLogGlue> access_log;

    bool should_exit = false;
    ShutdownListener shutdown_listener;
    SignalEvent sighup_event;

    /* stock */
    Balancer *balancer;
    StockMap *tcp_stock;
    TcpBalancer *tcp_balancer;

    Stock *pipe_stock;

    explicit LbInstance(const LbConfig &_config);
    ~LbInstance();

    /**
     * Transition the current process from "master" to "worker".  Call
     * this after forking in the new worker process.
     */
    void InitWorker();

    void InitAllListeners();
    void DeinitAllListeners();

    void InitAllControls();
    void EnableAllControls();
    void DeinitAllControls();

    /**
     * Create monitors for all members of all active clusters (from
     * #LbGotoMap).
     */
    void CreateMonitors();

    /**
     * Compress memory allocators, try to return unused memory areas
     * to the kernel.
     */
    void Compress();

    CertCache &GetCertCache(const LbCertDatabaseConfig &cert_db_config);
    void ConnectCertCaches();
    void DisconnectCertCaches();

    void FlushTranslationCaches() {
        goto_map.FlushCaches();
    }

    void InvalidateTranslationCaches(const TranslationInvalidateRequest &request) {
        goto_map.InvalidateTranslationCaches(request);
    }

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
