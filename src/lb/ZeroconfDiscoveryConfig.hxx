// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <avahi-common/address.h>


#include <memory>
#include <string>

class FileLineParser;

namespace Avahi {
class ErrorHandler;
class Client;
class ServiceExplorer;
class ServiceExplorerListener;
}

struct ZeroconfDiscoveryConfig {
	std::string service, domain, interface;

	AvahiProtocol protocol = AVAHI_PROTO_UNSPEC;

	bool IsEnabled() const noexcept {
		return !service.empty();
	}

	/**
	 * Parse a configuration file line.
	 *
	 * Throws on error.
	 *
	 * @return false if the word was not recognized
	 */
	bool ParseLine(const char *word, FileLineParser &line);

	/**
	 * Check whether the configuration is formally correct.
	 * Throws on error.
	 */
	void Check() const;

	/**
	 * Create a #ServiceExplorer instance for this configuration.
	 *
	 * IsEnabled() must be true.
	 *
	 * Throws on error.
	 */
	std::unique_ptr<Avahi::ServiceExplorer> Create(Avahi::Client &client,
						       Avahi::ServiceExplorerListener &listener,
						       Avahi::ErrorHandler &error_handler) const;
};
