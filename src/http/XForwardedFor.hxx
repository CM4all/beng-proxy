// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

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
	 * The "X-Forwarded-For" entries of all proxy servers on these
	 * interfaces will be trusted.
	 */
	std::set<std::string, std::less<>> trust_interfaces;

	[[gnu::pure]]
	bool IsTrustedHost(std::string_view host) const noexcept;

	[[gnu::pure]]
	std::string_view GetRealRemoteHost(const char *xff) const noexcept;
};
