// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Goto.hxx"
#include "Protocol.hxx"
#include "stats/HttpStats.hxx"
#include "io/Logger.hxx"
#include "fs/Listener.hxx"
#include "net/StaticSocketAddress.hxx"

#include <memory>

struct LbListenerConfig;
struct LbInstance;
class LbGotoMap;
class ClientAccountingMap;
class AccessLogGlue;
namespace Avahi { struct Service; }

/**
 * Listener on a TCP port.
 */
class LbListener final : FilteredSocketListenerHandler {
	LbInstance &instance;

	const LbListenerConfig &config;

	HttpStats http_stats;

	AccessLogGlue *const access_logger;

	FilteredSocketListener listener;

#ifdef HAVE_AVAHI
	std::unique_ptr<Avahi::Service> avahi_service;
#endif

	LbGoto destination;

	const Logger logger;

	const LbProtocol protocol;

	std::unique_ptr<ClientAccountingMap> client_accounting;

public:
	LbListener(LbInstance &_instance,
		   AccessLogGlue *_access_logger,
		   const LbListenerConfig &_config);

	~LbListener() noexcept;

	auto &GetEventLoop() const noexcept {
		return listener.GetEventLoop();
	}

#ifdef HAVE_AVAHI
	void SetZeroconfVisible(bool _visible) noexcept;
#endif

	auto GetLocalAddress() const noexcept {
		return listener.GetSocket().GetLocalAddress();
	}

	LbProtocol GetProtocol() const noexcept {
		return protocol;
	}

	const auto &GetConfig() const noexcept {
		return config;
	}

	HttpStats &GetHttpStats() noexcept {
		return http_stats;
	}

	const HttpStats *GetHttpStats() const noexcept {
		return protocol == LbProtocol::HTTP ? &http_stats : nullptr;
	}

	AccessLogGlue *GetAccessLogger() const noexcept {
		return access_logger;
	}

	void Scan(LbGotoMap &goto_map);

private:
	std::unique_ptr<Avahi::Service> MakeAvahiService() const noexcept;

	/* virtual methods from class FilteredSocketListenerHandler */
	UniqueSocketDescriptor OnFilteredSocketAccept(UniqueSocketDescriptor s,
						      SocketAddress address) override;

	void OnFilteredSocketConnect(PoolPtr pool,
				     UniquePoolPtr<FilteredSocket> socket,
				     SocketAddress address,
				     const SslFilter *ssl_filter) noexcept override;
	void OnFilteredSocketError(std::exception_ptr e) noexcept override;

};
