// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "PInstance.hxx"
#include "GotoMap.hxx"
#include "MonitorManager.hxx"
#include "access_log/Multi.hxx"
#include "stats/HttpStats.hxx"
#include "lib/avahi/ErrorHandler.hxx"
#include "memory/SlicePool.hxx"
#include "event/FarTimerEvent.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "net/FailureManager.hxx"
#include "io/Logger.hxx"
#include "io/StateDirectories.hxx"
#include "util/IntrusiveList.hxx"
#include "lb_features.h"

#include <forward_list>
#include <memory>
#include <map>

struct UidGid;
class PipeStock;
class BalancerMap;
class FilteredSocketStock;
class FilteredSocketBalancer;
class SslClientFactory;
struct LbConfig;
struct LbCertDatabaseConfig;
struct LbHttpConnection;
class LbTcpConnection;
class LbControl;
class LbListener;
class CertCache;
namespace BengControl { struct Stats; }
namespace Avahi { class Client; class Publisher; struct Service; }
namespace Prometheus { struct Stats; }

struct LbInstance final : PInstance, Avahi::ErrorHandler {
	const LbConfig &config;

	const Logger logger;

	ShutdownListener shutdown_listener;
	SignalEvent sighup_event;

	FarTimerEvent compress_event;

	const StateDirectories state_directories;

	HttpStats http_stats;

	std::forward_list<LbControl> controls;

	/**
	 * An allocator for per-request memory.
	 */
	SlicePool request_slice_pool{8192, 8192, "Requests"};

	/* stock */
	FailureManager failure_manager;
	std::unique_ptr<BalancerMap> balancer;

	std::unique_ptr<FilteredSocketStock> fs_stock;
	std::unique_ptr<FilteredSocketBalancer> fs_balancer;

	std::unique_ptr<SslClientFactory> ssl_client_factory;

	std::unique_ptr<PipeStock> pipe_stock;

	LbMonitorManager monitors;

#ifdef HAVE_AVAHI
	std::unique_ptr<Avahi::Client> avahi_client;
	std::unique_ptr<Avahi::Publisher> avahi_publisher;
#endif

	LbGotoMap goto_map;

	std::forward_list<LbListener> listeners;

#ifdef ENABLE_CERTDB
	std::map<std::string, CertCache> cert_dbs;
#endif

	IntrusiveList<
		LbHttpConnection,
		IntrusiveListBaseHookTraits<LbHttpConnection>,
		IntrusiveListOptions{.constant_time_size = true}> http_connections;

	IntrusiveList<
		LbTcpConnection,
		IntrusiveListBaseHookTraits<LbTcpConnection>,
		IntrusiveListOptions{.constant_time_size = true}> tcp_connections;

	MultiAccessLogGlue access_log;

	explicit LbInstance(const LbConfig &_config);
	~LbInstance() noexcept;

	auto &GetEventLoop() const noexcept {
		return shutdown_listener.GetEventLoop();
	}

	/**
	 * Transition the current process from "master" to "worker".  Call
	 * this after forking in the new worker process.
	 */
	void InitWorker();

	void InitAllListeners(const UidGid *logger_user);
	void DeinitAllListeners() noexcept;

	void InitAllControls();
	void EnableAllControls() noexcept;
	void DeinitAllControls() noexcept;

	[[gnu::pure]]
	Prometheus::Stats GetStats() const noexcept;

	void ReloadState() noexcept;

	/**
	 * Compress memory allocators, try to return unused memory areas
	 * to the kernel.
	 */
	void Compress() noexcept;

#ifdef ENABLE_CERTDB
	CertCache &GetCertCache(const LbCertDatabaseConfig &cert_db_config);
	void ConnectCertCaches();
	void DisconnectCertCaches() noexcept;
#endif

	void FlushTranslationCaches() noexcept {
		goto_map.FlushCaches();
	}

	void InvalidateTranslationCaches(const TranslationInvalidateRequest &request) noexcept {
		goto_map.InvalidateTranslationCaches(request);
	}

	void ShutdownCallback() noexcept;

	void ReloadEventCallback(int signo) noexcept;

#ifdef HAVE_AVAHI
	Avahi::Client &GetAvahiClient();
	Avahi::Publisher &GetAvahiPublisher();
#endif

private:
	void OnCompressTimer() noexcept;

	/* virtual methods from class Avahi::ErrorHandler */
	bool OnAvahiError(std::exception_ptr e) noexcept override;
};

void
init_signals(LbInstance *instance);

void
deinit_signals(LbInstance *instance);
