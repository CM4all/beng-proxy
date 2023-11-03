// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Config.hxx"
#include "lib/openssl/Error.hxx"
#include "pg/BinaryValue.hxx"

#include <openssl/aes.h>
#include <openssl/opensslv.h>

#include <memory>

/* the AES_wrap_key() API was deprecated in OpenSSL 3.0.0, but its
   replacement is more complicated, so let's ignore the warnings until
   we have migrated to libsodium */
// TODO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

class WrapKeyHelper {
	AES_KEY buffer;

public:
	AES_KEY *SetEncryptKey(const CertDatabaseConfig::AES256 &key) {
		if (AES_set_encrypt_key(key.data(), sizeof(key) * 8, &buffer) != 0)
			throw SslError("AES_set_encrypt_key() failed");

		return &buffer;
	}

	AES_KEY *SetEncryptKey(const CertDatabaseConfig &config,
			       const std::string &name) {
		const auto i = config.wrap_keys.find(name);
		if (i == config.wrap_keys.end())
			throw std::runtime_error("No such wrap_key: " + name);

		return SetEncryptKey(i->second);
	}

	std::pair<const char *,
		  AES_KEY *> SetEncryptKey(const CertDatabaseConfig &config) {
		if (config.default_wrap_key.empty())
			return std::make_pair(nullptr, nullptr);

		return std::make_pair(config.default_wrap_key.c_str(),
				      SetEncryptKey(config, config.default_wrap_key));
	}

	AES_KEY *SetDecryptKey(const CertDatabaseConfig::AES256 &key) {
		if (AES_set_decrypt_key(key.data(), sizeof(key) * 8, &buffer) != 0)
			throw SslError("AES_set_decrypt_key() failed");

		return &buffer;
	}

	AES_KEY *SetDecryptKey(const CertDatabaseConfig &config,
			       const std::string &name) {
		const auto i = config.wrap_keys.find(name);
		if (i == config.wrap_keys.end())
			throw std::runtime_error("No such wrap_key: " + name);

		return SetDecryptKey(i->second);
	}
};

#pragma GCC diagnostic pop

Pg::BinaryValue
WrapKey(Pg::BinaryValue key_der, AES_KEY *wrap_key,
	std::unique_ptr<std::byte[]> &wrapped);

Pg::BinaryValue
UnwrapKey(Pg::BinaryValue key_der,
	  const CertDatabaseConfig &config, const std::string &key_wrap_name,
	  std::unique_ptr<std::byte[]> &unwrapped);
