// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AddressList.hxx"
#include "AllocatorPtr.hxx"

AddressList::AddressList(AllocatorPtr alloc, const AddressList &src) noexcept
	:sticky_mode(src.sticky_mode)
{
	auto *p = alloc.NewArray<SocketAddress>(src.size());
	addresses = {p, src.size()};

	std::transform(src.begin(), src.end(), p, [&alloc](const auto &i){
		return alloc.Dup(i);
	});
}
