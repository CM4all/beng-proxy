// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "ssl/Config.hxx"
#include "net/LocalSocketAddress.hxx"
#include "net/SocketConfig.hxx"
#include "config.h"

#ifdef HAVE_AVAHI
#include "lib/avahi/ServiceConfig.hxx"
#endif

#include <forward_list>
#include <string>

struct BpListenerConfig : SocketConfig {
	std::string tag;

#ifdef HAVE_AVAHI
	Avahi::ServiceConfig zeroconf;
#endif

	std::string access_logger_name;

	/**
	 * If non-empty, then this listener has its own
	 * translation server(s) and doesn't use the global
	 * server.
	 */
	std::forward_list<LocalSocketAddress> translation_sockets;

	SslConfig ssl_config;

	enum class Handler {
		TRANSLATION,
		PROMETHEUS_EXPORTER,
	} handler = Handler::TRANSLATION;

	/**
	 * Enable or disable the access logger.
	 */
	bool access_logger = true;

	bool access_logger_only_errors = false;

	bool auth_alt_host = false;

	bool ssl = false;

	BpListenerConfig() noexcept
		:SocketConfig{
			.listen = 4096,
			.tcp_defer_accept = 10,
			.tcp_no_delay = true,
		}
	{
	}

	explicit BpListenerConfig(SocketAddress _address) noexcept
		:SocketConfig{
			.bind_address = AllocatedSocketAddress{_address},
			.listen = 4096,
			.tcp_defer_accept = 10,
			.tcp_no_delay = true,
		}
	{
	}
};
