// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FailureManagerProxy.hxx"
#include "net/FailureManager.hxx"
#include "net/SocketAddress.hxx"
#include "time/Expiry.hxx"

ReferencedFailureInfo &
FailureManagerProxy::MakeFailureInfo(SocketAddress address) const noexcept
{
	return failure_manager.Make(address);
}

bool
FailureManagerProxy::Check(const Expiry now, SocketAddress address,
			   bool allow_fade) const noexcept {
	return failure_manager.Check(now, address, allow_fade);
}
