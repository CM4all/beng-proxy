// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ZeroconfDiscoveryConfig.hxx"

bool
ZeroconfDiscoveryConfig::ParseLine(const char *word, FileLineParser &line)
{
	return Avahi::ServiceExplorerConfig::ParseLine(word, line);
}
