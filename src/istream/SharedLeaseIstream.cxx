// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SharedLeaseIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "util/SharedLease.hxx"

class SharedLeaseIstream final : public ForwardIstream {
	const SharedLease lease;

public:
	SharedLeaseIstream(struct pool &p, UnusedIstreamPtr &&_input,
			   SharedLease &&_lease) noexcept
		:ForwardIstream(p, std::move(_input)),
		 lease(std::move(_lease)) {}
};

UnusedIstreamPtr
NewSharedLeaseIstream(struct pool &pool, UnusedIstreamPtr &&input,
		      SharedLease &&lease) noexcept
{
	return NewIstreamPtr<SharedLeaseIstream>(pool, std::move(input),
						 std::move(lease));
}
