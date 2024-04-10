// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "XForwardedFor.hxx"
#include "net/IPv4Address.hxx"
#include "util/StringSplit.hxx"
#include "util/StringStrip.hxx"

#include <algorithm> // for std::any_of()

bool
XForwardedForConfig::IsTrustedHost(std::string_view host) const noexcept
{
	if (trust.contains(host))
		return true;

	if (const auto [address, interface] = Split(host, '%');
	    !address.empty() && !interface.empty() &&
	    trust_interfaces.contains(std::string_view{interface}))
		return true;

	return false;
}

bool
XForwardedForConfig::IsTrustedAddress(SocketAddress address) const noexcept
{
	IPv4Address ipv4;
	if (address.IsV4Mapped()) {
		ipv4 = address.UnmapV4();
		address = ipv4;
	}

	return std::any_of(trust_networks.begin(), trust_networks.end(), [address](const auto &i){
		return i.Matches(address);
	});
}

/**
 * Extract the right-most item of a comma-separated list, such as an
 * X-Forwarded-For header value.  Returns the remaining string and the
 * right-most item as a std::pair.
 */
[[gnu::pure]]
static std::pair<std::string_view, std::string_view>
LastListItem(std::string_view list) noexcept
{
	auto [a, b] = SplitLast(list, ',');
	if (b.data() == nullptr) {
		// no comma found
		a = Strip(a);
		if (a.empty())
			return {a, b};

		return {b, a};
	}

	b = Strip(b);
	return {a, b};
}

std::string_view
XForwardedForConfig::GetRealRemoteHost(const char *xff) const noexcept
{
	std::string_view list{xff};
	std::string_view result{};

	while (true) {
		auto l = LastListItem(list);
		if (l.second.empty())
			/* list finished; return the last good address (even if
			   it's a trusted proxy) */
			return result;

		result = l.second;
		if (!IsTrustedHost(result))
			/* this address is not a trusted proxy; return it */
			return result;

		list = l.first;
	}
}
