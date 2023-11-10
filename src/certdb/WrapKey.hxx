// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

template<typename> class AllocatedArray;

using WrapKeyBuffer = std::array<std::byte, 32>;
using WrapKeyView = std::span<const std::byte, 32>;

class WrapKey {
	WrapKeyBuffer key;

public:
	explicit constexpr WrapKey(WrapKeyView src) noexcept {
		std::copy(src.begin(), src.end(), key.begin());
	}

	AllocatedArray<std::byte> Encrypt(std::span<const std::byte> src) const;
	AllocatedArray<std::byte> Decrypt(std::span<const std::byte> src) const;
};
