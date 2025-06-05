// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "InterfaceNameCache.hxx"
#include "net/UniqueSocketDescriptor.hxx"

#include <fmt/core.h>

#include <map>
#include <string>

#include <sys/ioctl.h>
#include <net/if.h>

/**
 * A cache that maps interface indexes to names.
 */
static std::map<unsigned, std::string> interface_name_cache;

void
FlushInterfaceNameCache() noexcept
{
	interface_name_cache.clear();
}

std::string_view
GetCachedInterfaceName(unsigned index) noexcept
{
	if (auto i = interface_name_cache.find(index); i != interface_name_cache.end())
		return i->second;

	UniqueSocketDescriptor s;
	if (!s.Create(AF_UNIX, SOCK_DGRAM, 0))
		return {};

	struct ifreq r{};
	r.ifr_ifindex = index;

	if (ioctl(s.Get(), SIOCGIFNAME, &r) < 0)
		return {};

	return interface_name_cache.emplace(index, r.ifr_name).first->second;
}
