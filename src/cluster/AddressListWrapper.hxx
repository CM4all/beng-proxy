// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "AddressList.hxx"
#include "FailureManagerProxy.hxx"

#include <span>

/**
 * Wraps a std::span<const SocketAddress> in an interface for
 * PickFailover() and PickModulo().
 */
class AddressListWrapper : public AddressList, public FailureManagerProxy {
public:
	constexpr AddressListWrapper(FailureManager &_failure_manager,
				     std::span<const SocketAddress> _list) noexcept
		:AddressList(ShallowCopy{}, StickyMode::NONE, _list),
		 FailureManagerProxy(_failure_manager) {}
};
