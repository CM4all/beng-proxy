// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AprMd5.hxx"
#include "util/SpanCast.hxx"
#include "util/StringCompare.hxx"

#include <openssl/md5.h>

#include <algorithm>
#include <concepts>

#if defined(__GNUC__)
/* the MD5 API is deprecated in OpenSSL 3.0, but we want to keep using
   it */
// TODO
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using std::string_view_literals::operator""sv;

/**
 * A C++ wrapper for libcrypto's MD5_CTX.
 */
class CryptoMD5 {
	MD5_CTX ctx;

public:
	CryptoMD5() noexcept {
		MD5_Init(&ctx);
	}

	auto &Update(std::span<const std::byte> b) noexcept {
		MD5_Update(&ctx, b.data(), b.size());
		return *this;
	}

	auto &Update(std::string_view src) noexcept {
		return Update(AsBytes(src));
	}

	std::array<std::byte, MD5_DIGEST_LENGTH> Final() noexcept {
		std::array<std::byte, MD5_DIGEST_LENGTH> md;
		MD5_Final((unsigned char *)md.data(), &ctx);
		return md;
	}
};

static constexpr auto apr1_id = "$apr1$"sv;

[[gnu::pure]]
static std::string_view
ExtractSalt(const char *s) noexcept
{
	if (auto after_apr1_id = StringAfterPrefix(s, apr1_id))
		s = after_apr1_id;

	const char *dollar = strchr(s, '$');
	size_t length = dollar != nullptr
		? size_t(dollar - s)
		: strlen(s);

	return {s, std::min<size_t>(length, 8)};
}

static char *
To64(char *s, std::integral auto v, size_t n) noexcept
{
	static constexpr char itoa64[] =
		"./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

	for (size_t i = 0; i < n; ++i) {
		*s++ = itoa64[v & 0x3f];
		v >>= 6;
	}

	return s;
}

static char *
To64(char *s, std::byte v) noexcept
{
	return To64(s, static_cast<uint_fast8_t>(v), 2);
}

static char *
To64(char *s, std::byte a, std::byte b, std::byte c) noexcept
{
	return To64(s,
		    (static_cast<uint_fast32_t>(a) << 16) |
		    (static_cast<uint_fast32_t>(b) << 8) |
		    static_cast<uint_fast32_t>(c),
		    4);
}

bool
IsAprMd5(const char *crypted_password) noexcept
{
	return StringAfterPrefix(crypted_password, apr1_id) != nullptr;
}

StringBuffer<120>
AprMd5(const char *_pw, const char *_salt) noexcept
{
	const std::string_view pw{_pw};
	const auto salt = ExtractSalt(_salt);

	CryptoMD5 ctx;
	ctx.Update(pw);
	ctx.Update(apr1_id);
	ctx.Update(salt);

	auto f = CryptoMD5().Update(pw).Update(salt).Update(pw).Final();

	for (ssize_t i = pw.size(); i > 0; i -= MD5_DIGEST_LENGTH)
		ctx.Update(std::span{f.data(), std::min<size_t>(i, MD5_DIGEST_LENGTH)});

	for (size_t i = pw.size(); i != 0; i >>= 1) {
		if (i & 1)
			ctx.Update("\0"sv);
		else
			ctx.Update(pw.substr(0, 1));
	}

	f = ctx.Final();

	for (unsigned i = 0; i < 1000; ++i) {
		CryptoMD5 ctx2;
		if (i & 1)
			ctx2.Update(pw);
		else
			ctx2.Update(f);

		if (i % 3)
			ctx2.Update(salt);

		if (i % 7)
			ctx2.Update(pw);

		if (i & 1)
			ctx2.Update(f);
		else
			ctx2.Update(pw);

		f = ctx2.Final();
	}

	StringBuffer<120> result;
	char *p = result.data();
	p = std::copy(apr1_id.begin(), apr1_id.end(), p);
	p = std::copy(salt.begin(), salt.end(), p);
	*p++ = '$';

	p = To64(p, f[0], f[6], f[12]);
	p = To64(p, f[1], f[7], f[13]);
	p = To64(p, f[2], f[8], f[14]);
	p = To64(p, f[3], f[9], f[15]);
	p = To64(p, f[4], f[10], f[5]);
	p = To64(p, f[11]);
	*p = '\0';

	return result;
}
