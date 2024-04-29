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

	BpListenerConfig() noexcept {
		listen = 64;
		tcp_defer_accept = 10;
	}

	explicit BpListenerConfig(SocketAddress _address) noexcept
		:SocketConfig(_address)
	{
		listen = 64;
		tcp_defer_accept = 10;
		tcp_no_delay = true;
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
