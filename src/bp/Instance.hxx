/*
 * Copyright 2007-2021 CM4all GmbH
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

#pragma once

#include "PInstance.hxx"
#include "CommandLine.hxx"
#include "Config.hxx"
#include "event/SignalEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "event/TimerEvent.hxx"
#include "spawn/Registry.hxx"
#include "spawn/Handler.hxx"
#include "control/Handler.hxx"
#include "net/FailureManager.hxx"
#include "util/Background.hxx"

#ifdef HAVE_AVAHI
#include "avahi/Client.hxx"
#endif

#include <boost/intrusive/list.hpp>

#include <forward_list>
#include <memory>

class AccessLogGlue;
class WasStock;
class PipeStock;
class ResourceLoader;
class StockMap;
class TcpStock;
class TcpBalancer;
class SslClientFactory;
class FilteredSocketStock;
class FilteredSocketBalancer;
class SpawnService;
class ControlServer;
class LocalControl;
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
class NfsStock;
class NfsCache;
class HttpCache;
class FilterCache;
class UserAgentClassList;
namespace Uring { class Manager; }
class BPListener;
struct BpConnection;
namespace NgHttp2 { class Stock; }

struct BpInstance final : PInstance, ControlHandler, SpawnServerClientHandler {
	const BpConfig config;

	uint64_t http_request_counter = 0;
	uint64_t http_traffic_received_counter = 0;
	uint64_t http_traffic_sent_counter = 0;

	std::unique_ptr<UserAgentClassList> ua_classification;

#ifdef HAVE_URING
	std::unique_ptr<Uring::Manager> uring;
#endif

	std::forward_list<BPListener> listeners;

	boost::intrusive::list<BpConnection,
			       boost::intrusive::base_hook<boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>>,
			       boost::intrusive::constant_time_size<true>> connections;

	std::unique_ptr<AccessLogGlue> access_log, child_error_log;

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

	std::unique_ptr<SpawnServerClient> spawn;

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

#ifdef HAVE_AVAHI
	MyAvahiClient avahi_client;
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

	LhttpStock *lhttp_stock = nullptr;
	FcgiStock *fcgi_stock = nullptr;

#ifdef HAVE_LIBWAS
	WasStock *was_stock = nullptr;
#endif

	StockMap *delegate_stock = nullptr;

#ifdef HAVE_LIBNFS
	NfsStock *nfs_stock = nullptr;
	NfsCache *nfs_cache = nullptr;
#endif

	PipeStock *pipe_stock = nullptr;

	ResourceLoader *direct_resource_loader = nullptr;
	ResourceLoader *cached_resource_loader = nullptr;
	ResourceLoader *filter_resource_loader = nullptr;
	ResourceLoader *buffered_filter_resource_loader = nullptr;

	/* session */
	TimerEvent session_save_timer;

	explicit BpInstance(BpConfig &&_config) noexcept;
	~BpInstance() noexcept;

	gcc_pure
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
	void FadeTaggedChildren(const char *tag) noexcept;

	void ShutdownCallback() noexcept;

	void ReloadEventCallback(int signo) noexcept;

	void AddListener(const BpConfig::Listener &c);

	void EnableListeners() noexcept;
	void DisableListeners() noexcept;

	gcc_pure
	BengProxy::ControlStats GetStats() const noexcept;

	/* virtual methods from class ControlHandler */
	void OnControlPacket(ControlServer &control_server,
			     BengProxy::ControlCommand command,
			     ConstBuffer<void> payload,
			     WritableBuffer<UniqueFileDescriptor> fds,
			     SocketAddress address, int uid) override;

	void OnControlError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class SpawnServerClientHandler */
	void OnMemoryWarning(uint64_t memory_usage,
			     uint64_t memory_max) noexcept override;

private:
	bool AllocatorCompressCallback() noexcept;

	void SaveSessions() noexcept;

	void FreeStocksAndCaches() noexcept;
};
