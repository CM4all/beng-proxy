// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PDigestHeader.hxx"
#include "AllocatorPtr.hxx"
#include "lib/sodium/SHA256.hxx"
#include "util/HexFormat.hxx"

using std::string_view_literals::operator""sv;

[[gnu::pure]]
static auto
SHA256_Hex(std::span<const std::byte> src) noexcept
{
	const auto hash = SHA256(src);
	return HexFormat(std::span{hash});
}

const char *
GenerateDigestHeader(AllocatorPtr alloc, std::span<const std::byte> src) noexcept
{
	const auto hash_hex = SHA256_Hex(src);
	return alloc.Concat("sha-256="sv, hash_hex);
}
