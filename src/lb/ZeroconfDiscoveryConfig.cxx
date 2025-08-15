// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ZeroconfDiscoveryConfig.hxx"
#include "util/StringCompare.hxx"

using std::string_view_literals::operator""sv;

bool
ZeroconfDiscoveryConfig::ParseLine(const char *word, FileLineParser &line)
{
	if (const char *suffix = StringAfterPrefix(word, "zeroconf_"sv))
		return Avahi::ServiceExplorerConfig::ParseLine(suffix, line);
	else
		return false;
}
