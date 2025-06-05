// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "RefIstream.hxx"
#include "ForwardIstream.hxx"
#include "New.hxx"

class RefIstream final : public ForwardIstream {
public:
	RefIstream(struct pool &p, UnusedIstreamPtr _input)
		:ForwardIstream(p, std::move(_input)) {}
};

UnusedIstreamPtr
NewRefIstream(struct pool &pool, UnusedIstreamPtr input) noexcept
{
	return NewIstreamPtr<RefIstream>(pool, std::move(input));
}
