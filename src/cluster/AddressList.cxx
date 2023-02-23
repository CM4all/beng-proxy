// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AddressList.hxx"
#include "net/AddressInfo.hxx"
#include "AllocatorPtr.hxx"

AddressList::AddressList(ShallowCopy, AllocatorPtr alloc,
			 const AddressInfoList &src) noexcept
{
	const std::size_t n = std::distance(src.begin(), src.end());
	auto *p = alloc.NewArray<SocketAddress>(n);
	addresses = {p, n};

	std::copy(src.begin(), src.end(), p);
}

AddressList::AddressList(AllocatorPtr alloc, const AddressList &src) noexcept
	:sticky_mode(src.sticky_mode)
{
	auto *p = alloc.NewArray<SocketAddress>(src.size());
	addresses = {p, src.size()};

	std::transform(src.begin(), src.end(), p, [&alloc](const auto &i){
		return alloc.Dup(i);
	});
}
