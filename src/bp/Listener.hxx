// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "fs/Listener.hxx"
#include "net/StaticSocketAddress.hxx"
#include "config.h"

#include <memory>

struct BpInstance;
struct BpListenerConfig;
struct TaggedHttpStats;
class TranslationService;
class BpPrometheusExporter;
namespace Avahi { struct Service; }

/**
 * Listener for incoming HTTP connections.
 */
class BpListener final : FilteredSocketListenerHandler {
	BpInstance &instance;

	TaggedHttpStats &http_stats;

	const std::shared_ptr<TranslationService> translation_service;

	const std::unique_ptr<BpPrometheusExporter> prometheus_exporter;

	const char *const tag;

	const bool auth_alt_host;

	FilteredSocketListener listener;

#ifdef HAVE_AVAHI
	const std::unique_ptr<Avahi::Service> avahi_service;
#endif

public:
	BpListener(BpInstance &_instance,
		   TaggedHttpStats &_http_stats,
		   std::shared_ptr<TranslationService> _translation_service,
		   const BpListenerConfig &config,
		   UniqueSocketDescriptor _socket);
	~BpListener() noexcept;

	void Listen(UniqueSocketDescriptor &&_fd) noexcept {
		listener.Listen(std::move(_fd));
	}

#ifdef HAVE_AVAHI
	template<typename S>
	void SetAvahiService(S &&_avahi_service) noexcept {
		avahi_service = std::forward<S>(_avahi_service);
	}
#endif

	auto GetLocalAddress() const noexcept {
		return listener.GetLocalAddress();
	}

	void AddEvent() noexcept {
		listener.AddEvent();
	}

	void RemoveEvent() noexcept {
		listener.RemoveEvent();
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

	TranslationService &GetTranslationService() const noexcept {
		return *translation_service;
	}

private:
	std::unique_ptr<Avahi::Service> MakeAvahiService(const BpListenerConfig &config) const noexcept;

	/* virtual methods from class FilteredSocketListenerHandler */
	void OnFilteredSocketConnect(PoolPtr pool,
				     UniquePoolPtr<FilteredSocket> socket,
				     SocketAddress address,
				     const SslFilter *ssl_filter) noexcept override;
	void OnFilteredSocketError(std::exception_ptr e) noexcept override;

};
