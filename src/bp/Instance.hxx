/*
 * Copyright 2007-2018 Content Management AG
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

#ifndef BENG_PROXY_INSTANCE_HXX
#define BENG_PROXY_INSTANCE_HXX

#include "PInstance.hxx"
#include "CommandLine.hxx"
#include "Config.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "event/TimerEvent.hxx"
#include "spawn/Registry.hxx"
#include "control/Handler.hxx"
#include "avahi/Client.hxx"
#include "net/FailureManager.hxx"
#include "util/Background.hxx"

#include <boost/intrusive/list.hpp>

#include <forward_list>
#include <memory>

class AccessLogGlue;
class Stock;
class PipeStock;
class ResourceLoader;
class StockMap;
class TcpStock;
class TcpBalancer;
class FilteredSocketStock;
class FilteredSocketBalancer;
class SpawnService;
class ControlDistribute;
class ControlServer;
class LocalControl;
class SpawnServerClient;
class TranslateStock;
class LhttpStock;
struct FcgiStock;
struct NfsStock;
class NfsCache;
class HttpCache;
class FilterCache;
struct BpWorker;
class BPListener;
struct BpConnection;

struct BpInstance final : PInstance, ControlHandler {
    BpCmdLine cmdline;
    BpConfig config;

    uint64_t http_request_counter = 0;
    uint64_t http_traffic_received_counter = 0;
    uint64_t http_traffic_sent_counter = 0;

    std::forward_list<BPListener> listeners;

    boost::intrusive::list<BpConnection,
                           boost::intrusive::base_hook<boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
                           boost::intrusive::constant_time_size<true>> connections;

    std::unique_ptr<AccessLogGlue> access_log, child_error_log;

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

    std::unique_ptr<SpawnServerClient> spawn;

    boost::intrusive::list<BpWorker,
                           boost::intrusive::base_hook<boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
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
    std::unique_ptr<LocalControl> local_control_server;

    MyAvahiClient avahi_client;

    /* stock */
    FailureManager failure_manager;
    TranslateStock *translate_stock = nullptr;
    struct tcache *translate_cache = nullptr;
    TcpStock *tcp_stock = nullptr;
    TcpBalancer *tcp_balancer = nullptr;

    FilteredSocketStock *fs_stock = nullptr;
    FilteredSocketBalancer *fs_balancer = nullptr;

    /* cache */
    HttpCache *http_cache = nullptr;

    FilterCache *filter_cache = nullptr;

    LhttpStock *lhttp_stock = nullptr;
    FcgiStock *fcgi_stock = nullptr;

    StockMap *was_stock = nullptr;

    StockMap *delegate_stock = nullptr;

    NfsStock *nfs_stock = nullptr;
    NfsCache *nfs_cache = nullptr;

    PipeStock *pipe_stock = nullptr;

    ResourceLoader *direct_resource_loader = nullptr;
    ResourceLoader *cached_resource_loader = nullptr;
    ResourceLoader *filter_resource_loader = nullptr;
    ResourceLoader *buffered_filter_resource_loader = nullptr;

    /* session */
    TimerEvent session_save_timer;

    BpInstance() noexcept;
    ~BpInstance() noexcept;

    void EnableSignals() noexcept;
    void DisableSignals() noexcept;

    void ForkCow(bool inherit) noexcept;

    void Compress() noexcept;
    void ScheduleCompress() noexcept;
    void OnCompressTimer() noexcept;

    void ScheduleSaveSessions() noexcept;

    /**
     * Transition the current process from "master" to "worker".  Call
     * this after forking in the new worker process.
     */
    void InitWorker();

    pid_t SpawnWorker() noexcept;
    void ScheduleSpawnWorker() noexcept;
    void KillAllWorkers() noexcept;

    /**
     * Handler for #CONTROL_FADE_CHILDREN
     */
    void FadeChildren() noexcept;
    void FadeTaggedChildren(const char *tag) noexcept;

    void ShutdownCallback() noexcept;

    void ReloadEventCallback(int signo) noexcept;

    void AddListener(const BpConfig::Listener &c);
    void AddTcpListener(int port);

    void EnableListeners() noexcept;
    void DisableListeners() noexcept;

    gcc_pure
    BengProxy::ControlStats GetStats() const noexcept;

    /* virtual methods from class ControlHandler */
    void OnControlPacket(ControlServer &control_server,
                         BengProxy::ControlCommand command,
                         ConstBuffer<void> payload,
                         SocketAddress address) override;

    void OnControlError(std::exception_ptr ep) noexcept override;

private:
    void RespawnWorkerCallback() noexcept;

    bool AllocatorCompressCallback() noexcept;

    void SaveSessions() noexcept;

    void FreeStocksAndCaches() noexcept;
};

#endif
