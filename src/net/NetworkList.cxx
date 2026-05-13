// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "NetworkList.hxx"
#include "net/SocketAddress.hxx"
#include "AllocatorPtr.hxx"

#include <algorithm> // for std::any_of()

NetworkList::NetworkList(AllocatorPtr alloc, const NetworkList &src) noexcept
	:list(alloc.Dup(src.list))
{
}

bool
NetworkList::Contains(SocketAddress address) const noexcept
{
	return std::any_of(list.begin(), list.end(), [address](const auto &i){ return i.Matches(address); });
}
