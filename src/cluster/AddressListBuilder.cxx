// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AddressListBuilder.hxx"
#include "AddressList.hxx"
#include "net/AddressInfo.hxx"
#include "AllocatorPtr.hxx"

void
AddressListBuilder::Add(AllocatorPtr alloc, const SocketAddress address) noexcept
{
	v.push_back(alloc.Dup(address));
}

void
AddressListBuilder::Add(AllocatorPtr alloc, const AddressInfoList &list) noexcept
{
	for (const SocketAddress i : list)
		Add(alloc, i);
}

AddressList
AddressListBuilder::Finish(AllocatorPtr alloc) const noexcept
{
	return {
		ShallowCopy{},
		sticky_mode,
		alloc.Dup(std::span{v}),
	};
}
