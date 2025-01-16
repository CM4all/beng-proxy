// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PToString.hxx"
#include "pool/pool.hxx"
#include "net/SocketAddress.hxx"
#include "net/FormatAddress.hxx"
#include "net/IPv4Address.hxx"
#include "net/IPv6Address.hxx"

#include <fmt/core.h>

#include <arpa/inet.h>
#include <string.h>

static bool
V4HostToString(std::span<char> buffer, const IPv4Address &address) noexcept
{
	return inet_ntop(AF_INET, &address.GetAddress(), buffer.data(), buffer.size()) != nullptr;
}

static bool
V4ToString(std::span<char> buffer, const IPv4Address &address) noexcept
{
	const auto port = address.GetPort();
	if (port == 0)
		return V4HostToString(buffer, address);

	if (!V4HostToString(buffer.first(buffer.size() - 8), address))
		return false;

	buffer = buffer.subspan(strlen(buffer.data()));
	*fmt::format_to(buffer.data(), ":{}", port) = '\0';
	return true;
}

static bool
V6HostToString(std::span<char> buffer, const IPv6Address &address) noexcept
{
	return inet_ntop(AF_INET6, &address.GetAddress(), buffer.data(), buffer.size()) != nullptr;
}

static bool
V6HostWithScopeToString(std::span<char> buffer, const IPv6Address &address) noexcept
{
	const auto scope_id = address.GetScopeId();
	if (scope_id == 0)
		return V6HostToString(buffer, address);

	if (!V6HostToString(buffer.first(buffer.size() - 8), address))
		return false;

	buffer = buffer.subspan(strlen(buffer.data()));
	*fmt::format_to(buffer.data(), "%{}", scope_id) = '\0';
	return true;
}

static bool
V6ToString(std::span<char> buffer, const IPv6Address &address) noexcept
{
	const auto port = address.GetPort();
	if (port == 0)
		return V6HostWithScopeToString(buffer, address);

	buffer.front() = '[';
	buffer = buffer.subspan(1);

	if (!V6HostWithScopeToString(buffer.first(buffer.size() - 8), address))
		return false;

	buffer = buffer.subspan(strlen(buffer.data()));
	buffer.front() = ']';
	buffer = buffer.subspan(1);

	*fmt::format_to(buffer.data(), ":{}", port) = '\0';
	return true;
}

const char *
address_to_string(struct pool &pool, SocketAddress address)
{
	if (address.IsNull())
		return nullptr;

	char host[512];

	/* optimizations for IPv4 and IPv6 because glibc does not have
           NI_NUMERICSCOPE */
	switch (address.GetFamily()) {
	case AF_INET:
		if (!V4ToString(std::span{host}, IPv4Address::Cast(address)))
			return nullptr;
		break;

	case AF_INET6:
		if (!V6ToString(std::span{host}, IPv6Address::Cast(address)))
			return nullptr;
		break;

	default:
		if (!ToString(host, address) || *host == 0)
			return nullptr;
	}

	return p_strdup(&pool, host);
}

const char *
address_to_host_string(struct pool &pool, SocketAddress address)
{
	if (address.IsNull())
		return nullptr;

	char host[512];

	/* optimizations for IPv4 and IPv6 because glibc does not have
           NI_NUMERICSCOPE */
	switch (address.GetFamily()) {
	case AF_INET:
		if (!V4HostToString(std::span{host}, IPv4Address::Cast(address)))
			return nullptr;
		break;

	case AF_INET6:
		if (!V6HostWithScopeToString(std::span{host}, IPv6Address::Cast(address)))
			return nullptr;
		break;

	default:
		if (!HostToString(host, address) || *host == 0)
			return nullptr;
	}

	return p_strdup(&pool, host);
}
