// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <span>

std::span<const unsigned char>
FindAlpn(std::span<const unsigned char> haystack,
	 std::span<const unsigned char> needle) noexcept;
