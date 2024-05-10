// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PDigestHeader.hxx"
#include "AllocatorPtr.hxx"
#include "lib/sodium/SHA256.hxx"
#include "util/HexFormat.hxx"
#include "util/SpanCast.hxx"

#include <sodium/crypto_generichash.h>

using std::string_view_literals::operator""sv;

[[gnu::pure]]
static auto
GenericHashHex(std::span<const std::byte> src) noexcept
{
	const auto hash = SHA256(src);
	return HexFormat(std::span{hash});
}

const char *
GenerateDigestHeader(AllocatorPtr alloc, std::span<const std::byte> src) noexcept
{
	const auto hash_hex = GenericHashHex(src);
	return alloc.Concat("sha-256="sv, ToStringView(hash_hex));
}
