// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "fs/Listener.hxx"
#include "net/StaticSocketAddress.hxx"

#include <memory>

struct BpInstance;
struct SslConfig;
struct TaggedHttpStats;
class TranslationService;
class BpPrometheusExporter;

/**
 * Listener for incoming HTTP connections.
 */
class BPListener final : FilteredSocketListenerHandler {
	BpInstance &instance;

	TaggedHttpStats &http_stats;

	const std::shared_ptr<TranslationService> translation_service;

	const std::unique_ptr<BpPrometheusExporter> prometheus_exporter;

	const char *const tag;

	const bool auth_alt_host;

	FilteredSocketListener listener;

public:
	BPListener(BpInstance &_instance,
		   TaggedHttpStats &_http_stats,
		   std::shared_ptr<TranslationService> _translation_service,
		   const char *_tag,
		   bool _prometheus_exporter,
		   bool _auth_alt_host,
		   const SslConfig *ssl_config);
	~BPListener() noexcept;

	void Listen(UniqueSocketDescriptor &&_fd) noexcept {
		listener.Listen(std::move(_fd));
	}

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
	/* virtual methods from class FilteredSocketListenerHandler */
	void OnFilteredSocketConnect(PoolPtr pool,
				     UniquePoolPtr<FilteredSocket> socket,
				     SocketAddress address,
				     const SslFilter *ssl_filter) noexcept override;
	void OnFilteredSocketError(std::exception_ptr e) noexcept override;

};
