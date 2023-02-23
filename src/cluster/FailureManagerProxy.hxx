// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class Expiry;
class SocketAddress;
class FailureManager;
class ReferencedFailureInfo;

class FailureManagerProxy {
	FailureManager &failure_manager;

public:
	explicit constexpr FailureManagerProxy(FailureManager &_fm) noexcept
		:failure_manager(_fm) {}

	[[gnu::pure]]
	ReferencedFailureInfo &MakeFailureInfo(SocketAddress address) const noexcept;

	[[gnu::pure]]
	bool Check(const Expiry now, SocketAddress address,
		   bool allow_fade) const noexcept;
};
