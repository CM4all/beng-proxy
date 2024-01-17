// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "config.h"
#include "PInstance.hxx"
#include "CommandLine.hxx"
#include "UringGlue.hxx"
#include "Config.hxx"
#include "stats/TaggedHttpStats.hxx"
#include "lib/avahi/ErrorHandler.hxx"
#ifdef HAVE_LIBWAS
#include "was/MetricsHandler.hxx"
#endif
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "event/FarTimerEvent.hxx"
#include "spawn/ZombieReaper.hxx"
#include "spawn/Handler.hxx"
#include "event/net/control/Handler.hxx"
#include "net/FailureManager.hxx"
#include "io/FdCache.hxx"
#include "util/Background.hxx"
#include "util/IntrusiveList.hxx"

#include <forward_list>
#include <map>
#include <memory>

#ifdef HAVE_LIBWAS
#include <unordered_map>
#endif

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
class TranslationStock;
class TranslationCache;
class TranslationService;
class TranslationServiceBuilder;
class TranslationStockBuilder;
class TranslationCacheBuilder;
class MultiTranslationService;
class WidgetRegistry;
class LhttpStock;
class FcgiStock;
class HttpCache;
class FilterCache;
class EncodingCache;
class SessionManager;
class BPListener;
struct BpConnection;
namespace NgHttp2 { class Stock; }
namespace Avahi { class Client; class Publisher; }

struct BpInstance final : PInstance, BengControl::Handler, SpawnServerClientHandler,
#ifdef HAVE_LIBWAS
			  WasMetricsHandler,
#endif
			  Avahi::ErrorHandler {
	const BpConfig config;

	HttpStats http_stats;

	[[no_unique_address]]
	UringGlue uring{event_loop, config.use_io_uring};

	FdCache fd_cache{
		event_loop,
#ifdef HAVE_URING
		uring.get(),
#endif
	};

	std::map<std::string, TaggedHttpStats> listener_stats;

	std::forward_list<BPListener> listeners;

	IntrusiveList<
		BpConnection,
		IntrusiveListBaseHookTraits<BpConnection>,
		IntrusiveListOptions{.constant_time_size = true}> connections;

	std::unique_ptr<AccessLogGlue> access_log, child_error_log;

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
	SpawnService *spawn_service;

	std::unique_ptr<SpawnServerClient> spawn;

	std::unique_ptr<SessionManager> session_manager;

	/**
	 * The configured control channel servers (see
	 * BpConfig::control_listen).  May be empty if none was
	 * configured.
	 */
	std::forward_list<BengControl::Server> control_servers;

#ifdef HAVE_AVAHI
	std::unique_ptr<Avahi::Client> avahi_client;
	std::unique_ptr<Avahi::Publisher> avahi_publisher;
#endif

	/* stock */
	FailureManager failure_manager;

	std::unique_ptr<TranslationStockBuilder> translation_stocks;
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

	LhttpStock *lhttp_stock = nullptr;
	FcgiStock *fcgi_stock = nullptr;

#ifdef HAVE_LIBWAS
	WasStock *was_stock = nullptr;
	MultiWasStock *multi_was_stock = nullptr;
	RemoteWasStock *remote_was_stock = nullptr;

	std::unordered_map<std::string, float> was_metrics;
#endif

	StockMap *delegate_stock = nullptr;

	PipeStock *pipe_stock = nullptr;

	ResourceLoader *direct_resource_loader = nullptr;
	ResourceLoader *cached_resource_loader = nullptr;
	ResourceLoader *filter_resource_loader = nullptr;
	ResourceLoader *buffered_filter_resource_loader = nullptr;

	/* session */
	FarTimerEvent session_save_timer;

	explicit BpInstance(BpConfig &&_config) noexcept;
	~BpInstance() noexcept;

	[[gnu::pure]]
	TranslationServiceBuilder &GetTranslationServiceBuilder() const noexcept;

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

	void ShutdownCallback() noexcept;

	void FlushTranslationCaches() noexcept;

	void ReloadEventCallback(int signo) noexcept;

#ifdef HAVE_AVAHI
	Avahi::Client &GetAvahiClient();
	Avahi::Publisher &GetAvahiPublisher();
#endif

	void AddListener(const BpListenerConfig &c);

	void EnableListeners() noexcept;
	void DisableListeners() noexcept;

	[[gnu::pure]]
	BengControl::Stats GetStats() const noexcept;

	/* virtual methods from class BengControl::Handler */
	void OnControlPacket(BengControl::Server &control_server,
			     BengControl::Command command,
			     std::span<const std::byte> payload,
			     std::span<UniqueFileDescriptor> fds,
			     SocketAddress address, int uid) override;

	void OnControlError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class SpawnServerClientHandler */
	void OnMemoryWarning(uint64_t memory_usage,
			     uint64_t memory_max) noexcept override;

	/* virtual methods from class Avahi::ErrorHandler */
	bool OnAvahiError(std::exception_ptr e) noexcept override;

#ifdef HAVE_LIBWAS
	/* virtual methods from class WasMetricsHandler */
	void OnWasMetric(std::string_view name, float value) noexcept override;
#endif

private:
	bool AllocatorCompressCallback() noexcept;

	void SaveSessions() noexcept;

	void FreeStocksAndCaches() noexcept;
};
