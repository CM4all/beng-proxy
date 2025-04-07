// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "ssl/Config.hxx"
#include "net/LocalSocketAddress.hxx"
#include "net/SocketConfig.hxx"
#include "config.h"

#include <forward_list>
#include <string>

struct BpListenerConfig : SocketConfig {
	std::string tag;

#ifdef HAVE_AVAHI
	std::string zeroconf_service;
	std::string zeroconf_interface;
#endif

	std::string access_logger_name;

	/**
	 * If non-empty, then this listener has its own
	 * translation server(s) and doesn't use the global
	 * server.
	 */
	std::forward_list<LocalSocketAddress> translation_sockets;

	SslConfig ssl_config;

#ifdef HAVE_AVAHI
	/**
	 * The weight published via Zeroconf.  Negative value means
	 * don't publish a weight (peers will assume the default
	 * weight, i.e. 1.0).
	 */
	float zeroconf_weight = -1;
#endif

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

#ifdef HAVE_AVAHI
	/**
	 * @return the name of the interface where the
	 * Zeroconf service shall be published
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
};
