// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PToString.hxx"
#include "pool/pool.hxx"
#include "net/SocketAddress.hxx"
#include "net/FormatAddress.hxx"

const char *
address_to_string(struct pool &pool, SocketAddress address)
{
	if (address.IsNull())
		return nullptr;

	char host[512];
	bool success = ToString(host, address);
	if (!success || *host == 0)
		return nullptr;

	return p_strdup(&pool, host);
}

const char *
address_to_host_string(struct pool &pool, SocketAddress address)
{
	if (address.IsNull())
		return nullptr;

	char host[512];
	bool success = HostToString(host, address);
	if (!success || *host == 0)
		return nullptr;

	return p_strdup(&pool, host);
}
