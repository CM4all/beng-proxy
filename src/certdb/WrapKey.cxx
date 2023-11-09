// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "WrapKey.hxx"
#include "Config.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/openssl/Error.hxx"
#include "util/AllocatedArray.hxx"

#include <openssl/err.h>

#include <algorithm>

/* the AES_wrap_key() API was deprecated in OpenSSL 3.0.0, but its
   replacement is more complicated, so let's ignore the warnings until
   we have migrated to libsodium */
// TODO
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

WrapKey
WrapKey::MakeEncryptKey(const std::span<const std::byte, 32> src)
{
	WrapKey key;
	if (AES_set_encrypt_key(reinterpret_cast<const unsigned char *>(src.data()),
				src.size() * 8, &key.key) != 0)
		throw SslError("AES_set_encrypt_key() failed");

	return key;
}

WrapKey
WrapKey::MakeEncryptKey(const CertDatabaseConfig &config,
			std::string_view name)
{
	const auto i = config.wrap_keys.find(name);
	if (i == config.wrap_keys.end())
		throw FmtRuntimeError("No such wrap_key: {}", name);

	return MakeEncryptKey(i->second);
}

std::pair<const char *, WrapKey>
WrapKey::MakeEncryptKey(const CertDatabaseConfig &config)
{
	if (config.default_wrap_key.empty())
		return {};

	return {
		config.default_wrap_key.c_str(),
		MakeEncryptKey(config, config.default_wrap_key),
	};
}

WrapKey
WrapKey::MakeDecryptKey(const std::span<const std::byte, 32> src)
{
	WrapKey key;
	if (AES_set_decrypt_key(reinterpret_cast<const unsigned char *>(src.data()),
				src.size() * 8, &key.key) != 0)
		throw SslError("AES_set_decrypt_key() failed");

	return key;
}

WrapKey
WrapKey::MakeDecryptKey(const CertDatabaseConfig &config,
			std::string_view name)
{
	const auto i = config.wrap_keys.find(name);
	if (i == config.wrap_keys.end())
		throw FmtRuntimeError("No such wrap_key: {}", name);

	return MakeDecryptKey(i->second);
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
	int result = AES_wrap_key(&key, nullptr,
				  (unsigned char *)dest.data(),
				  (const unsigned char *)src.data(),
				  src.size());
	if (result <= 0)
		throw SslError("AES_wrap_key() failed");

	dest.SetSize(result);
	return dest;
}

AllocatedArray<std::byte>
WrapKey::Decrypt(std::span<const std::byte> src)
{
	if (src.size() <= 8)
		throw std::runtime_error("Malformed wrapped key");

	ERR_clear_error();

	AllocatedArray<std::byte> dest{src.size() - 8};
	int r = AES_unwrap_key(&key, nullptr,
			       (unsigned char *)dest.data(),
			       (const unsigned char *)src.data(),
			       src.size());
	if (r <= 0)
		throw SslError("AES_unwrap_key() failed");

	dest.SetSize(r);
	return dest;
}
