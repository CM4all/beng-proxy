// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "fs/Listener.hxx"
#include "net/StaticSocketAddress.hxx"
#include "util/IntrusiveList.hxx"
#include "config.h"

#include <memory>

struct BpConnection;
struct BpInstance;
struct BpListenerConfig;
struct BpListenerStats;
struct XForwardedForConfig;
class TranslationService;
class AccessLogGlue;
class BpPrometheusExporter;
namespace Avahi { struct Service; }

/**
 * Listener for incoming HTTP connections.
 */
class BpListener final : FilteredSocketListenerHandler {
	BpInstance &instance;

	BpListenerStats &http_stats;

	const XForwardedForConfig *const xff_config;

	AccessLogGlue *const access_logger;

	const std::shared_ptr<TranslationService> translation_service;

	const std::unique_ptr<BpPrometheusExporter> prometheus_exporter;

	const char *const tag;

	const bool auth_alt_host;

	const bool access_logger_only_errors;

	FilteredSocketListener listener;

#ifdef HAVE_AVAHI
	const std::unique_ptr<Avahi::Service> avahi_service;
#endif

	IntrusiveList<
		BpConnection,
		IntrusiveListBaseHookTraits<BpConnection>,
		IntrusiveListOptions{.constant_time_size = true}> connections;

public:
	BpListener(BpInstance &_instance,
		   BpListenerStats &_http_stats,
		   const XForwardedForConfig *_xff_config,
		   AccessLogGlue *_access_logger,
		   std::shared_ptr<TranslationService> _translation_service,
		   const BpListenerConfig &config,
		   UniqueSocketDescriptor _socket);
	~BpListener() noexcept;

	const XForwardedForConfig *GetXForwardedForConfig() const noexcept {
		return xff_config;
	}

	/**
	 * Returns the name used for loading settings from
	 * #StateDirectories.
	 */
	std::string_view GetStateName() const noexcept {
		if (tag != nullptr)
			return tag;

		return {};
	}

#ifdef HAVE_AVAHI
	bool HasZeroconf() const noexcept {
		return avahi_service != nullptr;
	}

	void SetZeroconfVisible(bool _visible) noexcept;
#endif // HAVE_AVAHI

	auto GetLocalAddress() const noexcept {
		return listener.GetSocket().GetLocalAddress();
	}

	auto &GetHttpStats() noexcept {
		return http_stats;
	}

	const char *GetTag() const noexcept {
		return tag;
	}

	bool GetAuthAltHost() const noexcept {
		return auth_alt_host;
	}

	AccessLogGlue *GetAccessLogger() const noexcept {
		return access_logger;
	}

	bool GetAccessLoggerOnlyErrors() const noexcept {
		return access_logger_only_errors;
	}

	TranslationService &GetTranslationService() const noexcept {
		return *translation_service;
	}

	std::size_t GetConnectionCount() const noexcept {
		return connections.size();
	}

	void CloseConnection(BpConnection &connection) noexcept;

	/**
	 * Drop client connections, starting with the lowest score
	 * (see http_server_connection_score()).  This is used to
	 * relieve some of the load on an overloaded machine
	 * (e.g. when the number of connections exceeds the configured
	 * limit).
	 *
	 * @return the number of connections which were dropped
 */	std::size_t DropSomeConnections() noexcept;

private:
	std::unique_ptr<Avahi::Service> MakeAvahiService(const BpListenerConfig &config) const noexcept;

	/* virtual methods from class FilteredSocketListenerHandler */
	void OnFilteredSocketConnect(PoolPtr pool,
				     UniquePoolPtr<FilteredSocket> socket,
				     SocketAddress address,
				     const SslFilter *ssl_filter) noexcept override;
	void OnFilteredSocketError(std::exception_ptr e) noexcept override;

};
