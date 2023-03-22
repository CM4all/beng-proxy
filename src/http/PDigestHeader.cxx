// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PDigestHeader.hxx"
#include "AllocatorPtr.hxx"
#include "util/HexFormat.hxx"
#include "util/SpanCast.hxx"

#include <sodium/crypto_generichash.h>

using std::string_view_literals::operator""sv;

[[gnu::pure]]
static auto
GenericHash(std::span<const std::byte> src) noexcept
{
	std::array<std::byte, crypto_generichash_BYTES> result;
	crypto_generichash(reinterpret_cast<unsigned char *>(result.data()),
			   result.size(),
			   reinterpret_cast<const unsigned char *>(src.data()),
			   src.size(),
			   nullptr, 0);
	return result;
}

[[gnu::pure]]
static auto
GenericHashHex(std::span<const std::byte> src) noexcept
{
	const auto hash = GenericHash(src);
	return HexFormat(std::span{hash});
}

const char *
GenerateDigestHeader(AllocatorPtr alloc, std::span<const std::byte> src) noexcept
{
	const auto hash_hex = GenericHashHex(src);

	/* "blake2b" is not an allowed value according to
	   https://www.iana.org/assignments/http-dig-alg/http-dig-alg.xhtml
	   but let's use it anyway, because it's the default algorithm
	   implemented by libsodium and we use this only for
	   experiments with our EncodingCache */
	return alloc.Concat("blake2b="sv, ToStringView(hash_hex));
}
