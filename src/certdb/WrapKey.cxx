// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "WrapKey.hxx"
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

AES_KEY *
WrapKeyHelper::SetEncryptKey(const std::span<const unsigned char, 32> key)
{
	if (AES_set_encrypt_key(key.data(), key.size() * 8, &buffer) != 0)
		throw SslError("AES_set_encrypt_key() failed");

	return &buffer;
}

AES_KEY *
WrapKeyHelper::SetEncryptKey(const CertDatabaseConfig &config,
			     std::string_view name)
{
	const auto i = config.wrap_keys.find(name);
	if (i == config.wrap_keys.end())
		throw FmtRuntimeError("No such wrap_key: {}", name);

	return SetEncryptKey(i->second);
}

std::pair<const char *, AES_KEY *>
WrapKeyHelper::SetEncryptKey(const CertDatabaseConfig &config)
{
	if (config.default_wrap_key.empty())
		return std::make_pair(nullptr, nullptr);

	return std::make_pair(config.default_wrap_key.c_str(),
			      SetEncryptKey(config, config.default_wrap_key));
}

AES_KEY *
WrapKeyHelper::SetDecryptKey(const std::span<const unsigned char, 32> key)
{
	if (AES_set_decrypt_key(key.data(), key.size() * 8, &buffer) != 0)
		throw SslError("AES_set_decrypt_key() failed");

	return &buffer;
}

AES_KEY *
WrapKeyHelper::SetDecryptKey(const CertDatabaseConfig &config,
			     std::string_view name)
{
	const auto i = config.wrap_keys.find(name);
	if (i == config.wrap_keys.end())
		throw FmtRuntimeError("No such wrap_key: {}", name);

	return SetDecryptKey(i->second);
}

AllocatedArray<std::byte>
WrapKey(std::span<const std::byte> src, AES_KEY *wrap_key)
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
	int result = AES_wrap_key(wrap_key, nullptr,
				  (unsigned char *)dest.data(),
				  (const unsigned char *)src.data(),
				  src.size());
	if (result <= 0)
		throw SslError("AES_wrap_key() failed");

	dest.SetSize(result);
	return dest;
}

AllocatedArray<std::byte>
UnwrapKey(std::span<const std::byte> src,
	  const CertDatabaseConfig &config, std::string_view key_wrap_name)
{
	if (src.size() <= 8)
		throw std::runtime_error("Malformed wrapped key");

	auto i = config.wrap_keys.find(key_wrap_name);
	if (i == config.wrap_keys.end())
		throw FmtRuntimeError("No such wrap_key: {}", key_wrap_name);

	WrapKeyHelper wrap_key_helper;
	const auto wrap_key =
		wrap_key_helper.SetDecryptKey(config, key_wrap_name);

	ERR_clear_error();

	AllocatedArray<std::byte> dest{src.size() - 8};
	int r = AES_unwrap_key(wrap_key, nullptr,
			       (unsigned char *)dest.data(),
			       (const unsigned char *)src.data(),
			       src.size());
	if (r <= 0)
		throw SslError("AES_unwrap_key() failed");

	dest.SetSize(r);
	return dest;
}
