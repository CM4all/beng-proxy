// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "GotoConfig.hxx"
#include "ssl/Config.hxx"
#include "net/SocketConfig.hxx"
#include "config.h"

#include <string>

struct LbCertDatabaseConfig;

struct LbListenerConfig : SocketConfig {
	std::string name;

	LbGotoConfig destination;

	std::string tag;

#ifdef HAVE_AVAHI
	std::string zeroconf_service;
	std::string zeroconf_interface;
#endif

	std::string access_logger_name;

	std::size_t max_connections_per_ip = 0;

	const LbCertDatabaseConfig *cert_db = nullptr;

	SslConfig ssl_config;

	/**
	 * Enable or disable the access logger.
	 */
	bool access_logger = true;

	bool access_logger_only_errors = false;

	bool verbose_response = false;

#ifdef HAVE_NGHTTP2
	bool force_http2 = false;

	bool alpn_http2 = true;
#endif

	bool ssl = false;

	bool hsts = false;

	explicit LbListenerConfig(const char *_name) noexcept
		:name(_name)
	{
		listen = 64;
		tcp_no_delay = true;
	}

#ifdef HAVE_AVAHI
	[[gnu::pure]]
	bool HasZeroconfPublisher() const noexcept {
		return !zeroconf_service.empty();
	}

	[[gnu::pure]]
	bool HasZeroConf() const noexcept {
		return destination.HasZeroConf();
	}

	/**
	 * @return the name of the interface where the Zeroconf
	 * service shall be published
	 */
	[[gnu::pure]]
	const char *GetZeroconfInterface() const noexcept {
		if (!zeroconf_interface.empty())
			return zeroconf_interface.c_str();

		if (!interface.empty())
			return interface.c_str();

		return nullptr;
	}
#endif

	bool GetAlpnHttp2() const noexcept {
#ifdef HAVE_NGHTTP2
		return destination.GetProtocol() == LbProtocol::HTTP &&
			alpn_http2;
#else
		return false;
#endif
	}
};
