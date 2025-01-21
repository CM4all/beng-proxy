// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "XForwardedFor.hxx"
#include "net/IPv4Address.hxx"
#include "net/IPv6Address.hxx"
#include "net/StaticSocketAddress.hxx"
#include "util/StringSplit.hxx"
#include "util/StringStrip.hxx"

#include <algorithm> // for std::any_of()

#include <arpa/inet.h> // for inet_pton()

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
	return std::any_of(trust_networks.begin(), trust_networks.end(), [address](const auto &i){
		return i.Matches(address);
	});
}

[[gnu::pure]]
static StaticSocketAddress
ParseIpAddress(std::string_view s) noexcept
{
	StaticSocketAddress result;
	result.Clear();

	if (s.front() == '[') {
		auto [t, rest] = Split(s.substr(1), ']');
		if (rest.data() == nullptr)
			return result;

		s = t;
	} else {
		auto [t, rest] = Split(s, ':');
		if (rest.find(':') == rest.npos)
			s = t;
	}

	char buffer[64];
	if (s.size() >= sizeof(buffer))
		return result;

	*std::copy(s.begin(), s.end(), buffer) = '\0';

	if (struct in_addr v4; inet_pton(AF_INET, buffer, &v4) == 1) {
		result = IPv4Address{v4, 0};
	} else if (struct in6_addr v6; inet_pton(AF_INET6, buffer, &v6) == 1) {
		result = IPv6Address{v6, 0};
	}

	return result;
}

bool
XForwardedForConfig::IsTrustedHostOrAddress(std::string_view host) const noexcept
{
	if (IsTrustedHost(host))
		return true;

	if (!trust_networks.empty()) {
		if (const auto address = ParseIpAddress(host);
		    address.IsDefined() && IsTrustedAddress(address))
			return true;
	}

	return false;
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
XForwardedForConfig::GetRealRemoteHost(std::string_view list) const noexcept
{
	std::string_view result{};

	while (true) {
		auto l = LastListItem(list);
		if (l.second.empty())
			/* list finished; return the last good address (even if
			   it's a trusted proxy) */
			return result;

		result = l.second;
		if (!IsTrustedHostOrAddress(result))
			/* this address is not a trusted proxy; return it */
			return result;

		list = l.first;
	}
}
