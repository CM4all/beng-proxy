// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lib/avahi/ExplorerConfig.hxx"

struct ZeroconfDiscoveryConfig : Avahi::ServiceExplorerConfig {
	/**
	 * Parse a configuration file line.
	 *
	 * Throws on error.
	 *
	 * @return false if the word was not recognized
	 */
	bool ParseLine(const char *word, FileLineParser &line);
};
