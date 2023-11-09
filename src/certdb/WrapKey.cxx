// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "WrapKey.hxx"

#include <openssl/err.h>

#include <algorithm>

/* the AES_wrap_key() API was deprecated in OpenSSL 3.0.0, but its
   replacement is more complicated, so let's ignore the warnings until
   we have migrated to libsodium */
// TODO
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

Pg::BinaryValue
WrapKey(Pg::BinaryValue src, AES_KEY *wrap_key,
	std::unique_ptr<std::byte[]> &wrapped)
{
	std::unique_ptr<std::byte[]> padded;
	size_t padded_size = ((src.size() - 1) | 7) + 1;
	if (padded_size != src.size()) {
		/* pad with zeroes */
		padded.reset(new std::byte[padded_size]);

		std::byte *p = padded.get();
		p = std::copy(src.begin(), src.end(), p);
		std::fill(p, padded.get() + padded_size, std::byte{});

		src = {padded.get(), padded_size};
	}

	wrapped.reset(new std::byte[src.size() + 8]);
	int result = AES_wrap_key(wrap_key, nullptr,
				  (unsigned char *)wrapped.get(),
				  (const unsigned char *)src.data(),
				  src.size());
	if (result <= 0)
		throw SslError("AES_wrap_key() failed");

	const std::size_t size(result);
	return {wrapped.get(), size};
}

Pg::BinaryValue
UnwrapKey(Pg::BinaryValue src,
	  const CertDatabaseConfig &config, std::string_view key_wrap_name,
	  std::unique_ptr<std::byte[]> &unwrapped)
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

	unwrapped.reset(new std::byte[src.size() - 8]);
	int r = AES_unwrap_key(wrap_key, nullptr,
			       (unsigned char *)unwrapped.get(),
			       (const unsigned char *)src.data(),
			       src.size());
	if (r <= 0)
		throw SslError("AES_unwrap_key() failed");

	const std::size_t size(r);
	return {unwrapped.get(), size};
}
