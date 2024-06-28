// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "net/MaskedSocketAddress.hxx"

#include <forward_list>
#include <string>
#include <set>

/**
 * Configuration which describes whether and how to log HTTP requests.
 */
struct XForwardedForConfig {
	/**
	 * A list of proxy servers whose "X-Forwarded-For" header will be
	 * trusted.
	 */
	std::set<std::string, std::less<>> trust;

	/**
	 * Like #trust, but contains a list of network addresses
	 * (IPv4/IPv6 address with netmask).
	 */
	std::forward_list<MaskedSocketAddress> trust_networks;

	/**
	 * The "X-Forwarded-For" entries of all proxy servers on these
	 * interfaces will be trusted.
	 */
	std::set<std::string, std::less<>> trust_interfaces;

	bool empty() const noexcept {
		return trust.empty() && trust_networks.empty() && trust_interfaces.empty();
	}

	[[gnu::pure]]
	bool IsTrustedHost(std::string_view host) const noexcept;

	[[gnu::pure]]
	bool IsTrustedAddress(SocketAddress address) const noexcept;

	[[gnu::pure]]
	std::string_view GetRealRemoteHost(const char *xff) const noexcept;
};
