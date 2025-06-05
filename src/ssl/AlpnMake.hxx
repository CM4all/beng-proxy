// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <array>

/**
 * Convert a string literal to an ALPN string (a std::array<unsigned
 * char>).
 */
template<std::size_t size>
static constexpr auto
MakeAlpnString(const char (&src)[size]) noexcept
{
	constexpr std::size_t length = size - 1;
	static_assert(length <= 0xff);

	std::array<unsigned char, size> dest;
	dest[0] = static_cast<unsigned char>(size - 1);
	for (std::size_t i = 0; i < size - 1; ++i)
		dest[i + 1] = static_cast<unsigned char>(src[i]);

	return dest;
}

static_assert(MakeAlpnString("abc").size() == 4);
static_assert(MakeAlpnString("abc").front() == 3);
static_assert(MakeAlpnString("abc").back() == 'c');

template<std::size_t size1, std::size_t size2>
static constexpr auto
ConcatAlpnStrings(const std::array<unsigned char, size1> &src1,
		  const std::array<unsigned char, size2> &src2) noexcept
{
	std::array<unsigned char, size1 + size2> dest;
	for (std::size_t i = 0; i < size1; ++i)
		dest[i] = src1[i];
	for (std::size_t i = 0; i < size2; ++i)
		dest[size1 + i] = src2[i];

	return dest;
}
