/*
 * Copyright 2007-2022 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
