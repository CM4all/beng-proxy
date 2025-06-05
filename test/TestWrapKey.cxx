// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "certdb/WrapKey.hxx"
#include "system/Urandom.hxx"
#include "util/AllocatedArray.hxx"
#include "util/SpanCast.hxx"

#include <gtest/gtest.h>

#include <cstdint>

using std::string_view_literals::operator""sv;

static void
TestWrapKey(const WrapKey &key,
	    std::span<const std::byte> msg,
	    std::span<const std::byte> expected_aes256)
{
	const auto secret_box = key.Encrypt(msg);
	EXPECT_EQ(ToStringView(key.Decrypt(secret_box)), ToStringView(msg));

	const auto aes256 = key.EncryptAES256(msg);

	if (expected_aes256.data() != nullptr) {
		EXPECT_EQ(ToStringView(aes256), ToStringView(expected_aes256));
	}

	EXPECT_EQ(ToStringView(key.DecryptAES256(aes256)), ToStringView(msg));
	EXPECT_EQ(ToStringView(key.Decrypt(aes256)), ToStringView(msg));
}

/**
 * With an all-zero AES256 key.
 */
TEST(WrapKey, ZeroKey)
{
	static constexpr WrapKeyBuffer key{};

	TestWrapKey(WrapKey{key},
		    AsBytes("0123456789abcdef"sv),
		    AsBytes("\x0a\x9f\xd3\x11\xc4\xbf\xfb\xa1\x3d\x64\x4c\x7b\x33\x7a\x3c\xa9\x69\xdc\x82\x71\xbb\x4a\xe7\xcb"sv));
}

/**
 * With a pregenerated AES256 key.
 */
TEST(WrapKey, PregeneratedKey)
{
	static constexpr std::array<uint8_t, 32> key{
		0xe8, 0x3c, 0x44, 0x2f, 0x75, 0x4b, 0x0d, 0x06,
		0x49, 0xe0, 0xe7, 0xdb, 0xcc, 0x88, 0x5a, 0xf7,
		0x8a, 0x38, 0xbf, 0x38, 0x53, 0x10, 0x9b, 0xc9,
		0x82, 0x29, 0xbe, 0x43, 0x18, 0xf2, 0x7c, 0x35,
	};

	TestWrapKey(WrapKey{std::as_bytes(std::span{key})},
		    AsBytes("0123456789abcdef"sv),
		    AsBytes("\x4e\xa6\x02\xe1\xb5\x7c\xf6\x88\x6a\xf5\x59\x73\xfa\x08\xc9\xb7\x1c\xf1\x8d\x78\x24\x5a\x65\xfd"sv));
}

/**
 * With a pregenerated AES256 key.
 */
TEST(WrapKey, RandomKey)
{
	WrapKeyBuffer key;
	UrandomFill(key);

	TestWrapKey(WrapKey{key},
		    AsBytes("0123456789abcdef"sv), {});
}
