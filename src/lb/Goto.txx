// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Goto.hxx"
#include "Branch.hxx"

template<typename C, typename R>
const LbGoto &
LbGoto::FindRequestLeaf(const C &connection, const R &request) const noexcept
{
	if (auto *branch = std::get_if<LbBranch *>(&destination))
		return (*branch)->FindRequestLeaf(connection, request);

	return *this;
}
