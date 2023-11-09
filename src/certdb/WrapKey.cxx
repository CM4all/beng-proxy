// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "WrapKey.hxx"
#include "Config.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/UniqueEVP.hxx"
#include "util/AllocatedArray.hxx"

#include <openssl/err.h>

#include <algorithm>

WrapKey
WrapKey::Make(const CertDatabaseConfig &config,
	      std::string_view name)
{
	const auto i = config.wrap_keys.find(name);
	if (i == config.wrap_keys.end())
		throw FmtRuntimeError("No such wrap_key: {}", name);

	return WrapKey{i->second};
}

std::pair<const char *, WrapKey>
WrapKey::MakeDefault(const CertDatabaseConfig &config)
{
	if (config.default_wrap_key.empty())
		return {nullptr, {}};

	return {
		config.default_wrap_key.c_str(),
		Make(config, config.default_wrap_key),
	};
}

AllocatedArray<std::byte>
WrapKey::Encrypt(std::span<const std::byte> src)
{
	AllocatedArray<std::byte> padded;
	size_t padded_size = ((src.size() - 1) | 7) + 1;
	if (padded_size != src.size()) {
		/* pad with zeroes */
		padded.ResizeDiscard(padded_size);

		auto p = padded.begin();
		p = std::copy(src.begin(), src.end(), p);
		std::fill(p, padded.end(), std::byte{});

		src = padded;
	}

	AllocatedArray<std::byte> dest{src.size() + 8};

	ERR_clear_error();

	UniqueEVP_CIPHER_CTX ctx{EVP_CIPHER_CTX_new()};
	if (!ctx)
		throw SslError{"EVP_CIPHER_CTX_new() failed"};

	EVP_CIPHER_CTX_set_flags(ctx.get(), EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);

	if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_wrap(), nullptr,
			       reinterpret_cast<const unsigned char *>(key.data()),
			       nullptr) != 1)
		throw SslError{"EVP_EncryptInit_ex() failed"};

	int outlen;
	if (EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(dest.data()), &outlen,
			      reinterpret_cast<const unsigned char *>(src.data()), src.size()) != 1)
		throw SslError{"EVP_EncryptUpdate() failed"};

	std::size_t dest_position = outlen;

	if (EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(dest.data() + dest_position), &outlen) != 1)
		throw SslError{"EVP_EncryptFinal_ex() failed"};

	dest_position += outlen;
	assert(dest_position <= dest.size());
	dest.SetSize(dest_position);
	return dest;
}

AllocatedArray<std::byte>
WrapKey::Decrypt(std::span<const std::byte> src)
{
	if (src.size() <= 8)
		throw std::runtime_error("Malformed wrapped key");

	AllocatedArray<std::byte> dest{src.size() - 8};

	ERR_clear_error();

	UniqueEVP_CIPHER_CTX ctx{EVP_CIPHER_CTX_new()};
	if (!ctx)
		throw SslError{"EVP_CIPHER_CTX_new() failed"};

	EVP_CIPHER_CTX_set_flags(ctx.get(), EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);

	if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_wrap(), nullptr,
			       reinterpret_cast<const unsigned char *>(key.data()),
			       nullptr) != 1)
		throw SslError{"EVP_DecryptInit_ex() failed"};

	int outlen;
	if (EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(dest.data()), &outlen,
			      reinterpret_cast<const unsigned char *>(src.data()), src.size()) != 1)
		throw SslError{"EVP_DecryptUpdate() failed"};

	std::size_t dest_position = outlen;

	if (EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(dest.data() + dest_position), &outlen) != 1)
		throw SslError{"EVP_DecryptFinal_ex() failed"};

	dest_position += outlen;
	assert(dest_position <= dest.size());
	dest.SetSize(dest_position);
	return dest;
}
