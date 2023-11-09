// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "certdb/WrapKey.hxx"
#include "certdb/Config.hxx"
#include "system/Urandom.hxx"
#include "util/AllocatedArray.hxx"
#include "util/SpanCast.hxx"

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

static void
TestWrapKeyAES256(const CertDatabaseConfig::AES256 &key,
		  std::span<const std::byte> msg,
		  std::span<const std::byte> expected)
{
	CertDatabaseConfig config;
	config.wrap_keys.emplace("foo"sv, key);

	WrapKeyHelper wrap_key;
	auto *aes = wrap_key.SetEncryptKey(key);

	const auto wrapped = WrapKey(msg, aes);

	if (expected.data() != nullptr) {
		EXPECT_EQ(ToStringView(wrapped), ToStringView(expected));
	}

	const auto unwrapped = UnwrapKey(wrapped, config, "foo"sv);

	EXPECT_EQ(ToStringView(msg), ToStringView(unwrapped));
}

/**
 * With an all-zero AES256 key.
 */
TEST(WrapKey, ZeroKey)
{
	static constexpr CertDatabaseConfig::AES256 key{};

	TestWrapKeyAES256(key, AsBytes("0123456789abcdef"sv),
			  AsBytes("\x0a\x9f\xd3\x11\xc4\xbf\xfb\xa1\x3d\x64\x4c\x7b\x33\x7a\x3c\xa9\x69\xdc\x82\x71\xbb\x4a\xe7\xcb"sv));
}

/**
 * With a pregenerated AES256 key.
 */
TEST(WrapKey, PregeneratedKey)
{
	static constexpr CertDatabaseConfig::AES256 key{
		0xe8, 0x3c, 0x44, 0x2f, 0x75, 0x4b, 0x0d, 0x06,
		0x49, 0xe0, 0xe7, 0xdb, 0xcc, 0x88, 0x5a, 0xf7,
		0x8a, 0x38, 0xbf, 0x38, 0x53, 0x10, 0x9b, 0xc9,
		0x82, 0x29, 0xbe, 0x43, 0x18, 0xf2, 0x7c, 0x35,
	};

	TestWrapKeyAES256(key, AsBytes("0123456789abcdef"sv),
			  AsBytes("\x4e\xa6\x02\xe1\xb5\x7c\xf6\x88\x6a\xf5\x59\x73\xfa\x08\xc9\xb7\x1c\xf1\x8d\x78\x24\x5a\x65\xfd"sv));
}

/**
 * With a pregenerated AES256 key.
 */
TEST(WrapKey, RandomKey)
{
	CertDatabaseConfig::AES256 key;
	UrandomFill(std::as_writable_bytes(std::span{key}));

	TestWrapKeyAES256(key, AsBytes("0123456789abcdef"sv), {});
}
