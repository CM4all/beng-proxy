// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "GotoConfig.hxx"
#include "ssl/Config.hxx"
#include "net/SocketConfig.hxx"
#include "config.h"

#ifdef HAVE_AVAHI
#include "lib/avahi/ServiceConfig.hxx"
#endif

#include <string>

struct LbCertDatabaseConfig;

struct LbListenerConfig : SocketConfig {
	std::string name;

	LbGotoConfig destination;

	std::string tag;

#ifdef HAVE_AVAHI
	Avahi::ServiceConfig zeroconf;
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
		listen = 4096;
		tcp_no_delay = true;
	}

#ifdef HAVE_AVAHI
	[[gnu::pure]]
	bool HasZeroconfPublisher() const noexcept {
		return zeroconf.IsEnabled();
	}

	[[gnu::pure]]
	bool HasZeroConf() const noexcept {
		return destination.HasZeroConf();
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
