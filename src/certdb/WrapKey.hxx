// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lib/sodium/SecretBoxTypes.hxx"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

template<typename> class AllocatedArray;

using WrapKeyBuffer = CryptoSecretBoxKey;
using WrapKeyView = CryptoSecretBoxKeyView;

class WrapKey {
	WrapKeyBuffer key;

public:
	explicit constexpr WrapKey(WrapKeyView src) noexcept {
		std::copy(src.begin(), src.end(), key.begin());
	}

	AllocatedArray<std::byte> EncryptAES256(std::span<const std::byte> src) const;
	AllocatedArray<std::byte> DecryptAES256(std::span<const std::byte> src) const;

	AllocatedArray<std::byte> Encrypt(std::span<const std::byte> src) const;
	AllocatedArray<std::byte> Decrypt(std::span<const std::byte> src) const;
};
