// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "net/MaskedInetAddress.hxx"
#include "util/TagStructs.hxx"

#include <cstdint>
#include <span>

class AllocatorPtr;
class SocketAddress;

class NetworkList final {
	std::span<const MaskedInetAddress> list;

public:
	NetworkList() noexcept = default;

	explicit constexpr NetworkList(std::span<const MaskedInetAddress> _list) noexcept
		:list(_list) {}

	constexpr NetworkList(ShallowCopy, const NetworkList &src) noexcept
		:list(src.list) {}

	NetworkList(AllocatorPtr alloc, const NetworkList &src) noexcept;

	constexpr bool empty() const noexcept {
		return list.empty();
	}

	[[gnu::pure]]
	bool Contains(SocketAddress address) const noexcept;
};
