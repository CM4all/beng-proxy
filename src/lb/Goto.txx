/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#pragma once

#include "Goto.hxx"
#include "Branch.hxx"

template<typename R>
const LbGoto &
LbGoto::FindRequestLeaf(const R &request) const
{
	if (branch != nullptr)
		return branch->FindRequestLeaf(request);

	return *this;
}
