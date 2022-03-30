/*
 * Copyright 2022 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ClientAccounting.hxx"
#include "net/SocketAddress.hxx"
#include "net/IPv4Address.hxx"
#include "net/IPv6Address.hxx"

AccountedClientConnection::~AccountedClientConnection() noexcept
{
	if (per_client != nullptr)
		per_client->RemoveConnection(*this);
}

static constexpr uint_least64_t
Read64(const uint8_t *src) noexcept
{
	uint_least64_t value{};
	for (unsigned i = 0; i < 8; ++i)
		value = (value << 8) | src[i];
	return value;
}

static constexpr uint_least64_t
ToInteger(const struct in6_addr &addr) noexcept
{
	return Read64(addr.s6_addr) ^ Read64(addr.s6_addr + 8);
}

static uint_least64_t
ToInteger(SocketAddress address) noexcept
{
	if (address.IsNull())
		return 0;

	switch (address.GetFamily()) {
	case AF_INET:
		return IPv4Address::Cast(address).GetNumericAddressBE();

	case AF_INET6:
		return ToInteger(IPv6Address::Cast(address).GetAddress());

	default:
		return 0;
	}
}

PerClientAccounting::PerClientAccounting(ClientAccountingMap &_map,
					 uint_least64_t _address) noexcept
	:map(_map), address(_address)
{
}

bool
PerClientAccounting::Check() const noexcept
{
	const std::size_t max_connections = map.GetMaxConnections();
	return max_connections == 0 || connections.size() < max_connections;
}

void
PerClientAccounting::AddConnection(AccountedClientConnection &c) noexcept
{
	assert(c.per_client == nullptr);

	connections.push_back(c);
	c.per_client = this;
}

void
PerClientAccounting::RemoveConnection(AccountedClientConnection &c) noexcept
{
	assert(c.per_client == this);

	connections.erase(connections.iterator_to(c));
	c.per_client = nullptr;
}

PerClientAccounting *
ClientAccountingMap::Get(SocketAddress _address) noexcept
{
	const uint_least64_t address = ToInteger(_address);
	if (address == 0)
		return nullptr;

	Map::insert_commit_data hint;
	auto [i, inserted] =
		map.insert_check(address, map.hash_function(), map.key_eq(),
				 hint);
	if (inserted) {
		auto *per_client = new PerClientAccounting(*this, address);
		i = map.insert_commit(*per_client, hint);
	}

	return &*i;

}
