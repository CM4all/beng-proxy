// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ConditionConfig.hxx"

bool
LbConditionConfig::MatchAddress(SocketAddress address) const noexcept
{
	return std::get<MaskedSocketAddress>(value).Matches(address);
}
