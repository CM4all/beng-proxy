// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "config.h"
#include "PInstance.hxx"
#include "CommandLine.hxx"
#include "UringGlue.hxx"
#include "Config.hxx"
#include "access_log/Multi.hxx"
#include "stats/HttpStats.hxx"
#include "lib/avahi/ErrorHandler.hxx"
#ifdef HAVE_LIBWAS
#include "was/MetricsHandler.hxx"
#endif
#include "memory/SlicePool.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "event/FarTimerEvent.hxx"
#include "spawn/ZombieReaper.hxx"
#include "event/net/control/Handler.hxx"
#include "net/FailureManager.hxx"
#include "io/uring/config.h" // for HAVE_URING
#include "io/FdCache.hxx"
#include "io/FileCache.hxx"
#include "io/StateDirectories.hxx"
#include "util/Background.hxx"

#include <forward_list>
#include <list>
#include <map>
#include <memory>

#ifdef HAVE_LIBWAS
#include <unordered_map>
#endif

struct UidGid;
class AccessLogGlue;
class WasStock;
class MultiWasStock;
class RemoteWasStock;
class PipeStock;
class ResourceLoader;
class StockMap;
class TcpStock;
class TcpBalancer;
class SslClientFactory;
class FilteredSocketStock;
class FilteredSocketBalancer;
class SpawnService;
namespace BengControl { class Server; }
class SpawnServerClient;
struct LaunchSpawnServerResult;
class CgroupMemoryThrottle;
class CgroupMultiWatch;
class CgroupPidsThrottle;
class TranslationStock;
class TranslationCache;
class TranslationService;
class TranslationServiceBuilder;
class TranslationStockBuilder;
class TranslationCacheBuilder;
class MultiTranslationService;
class WidgetRegistry;
class BpListenStreamStockHandler;
class ListenStreamStock;
class LhttpStock;
class FcgiStock;
class HttpCache;
class FilterCache;
class EncodingCache;
class SessionManager;
class BpListener;
class BpPerSite;
class BpPerSiteMap;
struct BpListenerStats;
namespace NgHttp2 { class Stock; }
namespace Avahi { class Client; class Publisher; }
namespace Prometheus { struct Stats; }
namespace Net::Log { class Sink; }

struct BpInstance final : PInstance, BengControl::Handler,
#ifdef HAVE_LIBWAS
			  WasMetricsHandler,
#endif
			  Avahi::ErrorHandler {
	const BpConfig config;

	HttpStats http_stats;

	[[no_unique_address]]
	UringGlue uring{
		event_loop,
		config.use_io_uring,
		config.io_uring_sqpoll,
		config.io_uring_sq_thread_cpu,
	};

#ifdef HAVE_URING
	FarTimerEvent enable_uring_timer{event_loop, BIND_THIS_METHOD(OnEnableUringTimer)};
#endif

	const StateDirectories state_directories;

	FdCache fd_cache{event_loop};

	/**
	 * Cache for READ_FILE.
	 */
	FileCache file_cache{event_loop};

	/**
	 * An allocator for per-request memory.
	 */
	SlicePool request_slice_pool{16384, 4096, "Requests"};

#ifdef HAVE_AVAHI
	std::unique_ptr<Avahi::Client> avahi_client;
	std::unique_ptr<Avahi::Publisher> avahi_publisher;
#endif

	std::map<std::string, BpListenerStats> listener_stats;

	std::list<BpListener> listeners;

	MultiAccessLogGlue access_log;

	std::unique_ptr<AccessLogGlue> child_error_log;

	ShutdownListener shutdown_listener;
	SignalEvent sighup_event;

	FarTimerEvent compress_timer;

	/**
	 * Registry for jobs running in background, created by the request
	 * handler code.
	 */
	BackgroundManager background_manager;

	/* child management */
	ZombieReaper zombie_reaper{event_loop};

	const std::unique_ptr<SpawnServerClient> spawn;

#ifdef HAVE_LIBSYSTEMD
	std::unique_ptr<CgroupMultiWatch> cgroup_multi_watch;
	std::unique_ptr<CgroupMemoryThrottle> cgroup_memory_throttle;
	std::unique_ptr<CgroupPidsThrottle> cgroup_pids_throttle;
#endif

	SpawnService *spawn_service;

	std::unique_ptr<SessionManager> session_manager;

	/**
	 * The configured control channel servers (see
	 * BpConfig::control_listen).  May be empty if none was
	 * configured.
	 */
	std::forward_list<BengControl::Server> control_servers;

	/* stock */
	FailureManager failure_manager;

	std::unique_ptr<TranslationStockBuilder> translation_clients;
	std::shared_ptr<MultiTranslationService> uncached_translation_service;

	std::unique_ptr<TranslationCacheBuilder> translation_caches;
	std::shared_ptr<MultiTranslationService> cached_translation_service;

	std::shared_ptr<TranslationService> translation_service;
	WidgetRegistry *widget_registry = nullptr;

	TcpStock *tcp_stock = nullptr;
	TcpBalancer *tcp_balancer = nullptr;

	std::unique_ptr<SslClientFactory> ssl_client_factory;

	FilteredSocketStock *fs_stock = nullptr;
	FilteredSocketBalancer *fs_balancer = nullptr;

#ifdef HAVE_NGHTTP2
	NgHttp2::Stock *nghttp2_stock = nullptr;
#endif

	/* cache */
	HttpCache *http_cache = nullptr;

	FilterCache *filter_cache = nullptr;

	std::unique_ptr<EncodingCache> encoding_cache;

	std::unique_ptr<BpListenStreamStockHandler> spawn_listen_stream_stock_handler;
	std::unique_ptr<ListenStreamStock> listen_stream_stock;

	std::unique_ptr<LhttpStock> lhttp_stock;
	std::unique_ptr<FcgiStock> fcgi_stock;

#ifdef HAVE_LIBWAS
	WasStock *was_stock = nullptr;
	MultiWasStock *multi_was_stock = nullptr;
	RemoteWasStock *remote_was_stock = nullptr;

	std::unordered_map<std::string, float> was_metrics;
#endif

	PipeStock *pipe_stock = nullptr;

	ResourceLoader *direct_resource_loader = nullptr;
	ResourceLoader *cached_resource_loader = nullptr;
	ResourceLoader *filter_resource_loader = nullptr;
	ResourceLoader *buffered_filter_resource_loader = nullptr;

	/* session */
	FarTimerEvent session_save_timer;

	std::unique_ptr<BpPerSiteMap> per_site;

	BpInstance(BpConfig &&_config,
		   LaunchSpawnServerResult &&spawner) noexcept;
	~BpInstance() noexcept;

	[[gnu::pure]]
	TranslationServiceBuilder &GetTranslationServiceBuilder() const noexcept;

	Net::Log::Sink *GetChildLogSink(const UidGid *logger_user);

	void EnableSignals() noexcept;
	void DisableSignals() noexcept;

	void ForkCow(bool inherit) noexcept;

	void Compress() noexcept;
	void ScheduleCompress() noexcept;
	void OnCompressTimer() noexcept;

	void ScheduleSaveSessions() noexcept;

	/**
	 * Handler for #CONTROL_FADE_CHILDREN
	 */
	void FadeChildren() noexcept;
	void FadeTaggedChildren(std::string_view tag) noexcept;

	void ReloadState() noexcept;

	void ShutdownCallback() noexcept;

	void FlushTranslationCaches() noexcept;

	void ReloadEventCallback(int signo) noexcept;

#ifdef HAVE_AVAHI
	Avahi::Client &GetAvahiClient();
	Avahi::Publisher &GetAvahiPublisher();
#endif

	void AddListener(const BpListenerConfig &c, const UidGid *logger_user);

	[[gnu::pure]]
	Prometheus::Stats GetStats() const noexcept;

	void HandleDisableUring(std::span<const std::byte> payload) noexcept;

	/* virtual methods from class BengControl::Handler */
	void OnControlPacket(BengControl::Command command,
			     std::span<const std::byte> payload,
			     std::span<UniqueFileDescriptor> fds,
			     SocketAddress address, int uid) override;

	void OnControlError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Avahi::ErrorHandler */
	bool OnAvahiError(std::exception_ptr e) noexcept override;

#ifdef HAVE_LIBWAS
	/* virtual methods from class WasMetricsHandler */
	void OnWasMetric(std::string_view name, float value) noexcept override;
#endif

	SharedLeasePtr<BpPerSite> MakePerSite(std::string_view site) noexcept;

private:
#ifdef HAVE_LIBSYSTEMD
	void HandleMemoryWarning() noexcept;
#endif

	bool AllocatorCompressCallback() noexcept;

	void SaveSessions() noexcept;

	void FreeStocksAndCaches() noexcept;

	void OnEnableUringTimer() noexcept;
	void DisableUringFor(Event::Duration duration) noexcept;
};
