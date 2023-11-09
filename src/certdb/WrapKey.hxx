// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Config.hxx"

#include <openssl/aes.h>

#include <cstddef>
#include <span>

template<typename> class AllocatedArray;

/* the AES_wrap_key() API was deprecated in OpenSSL 3.0.0, but its
   replacement is more complicated, so let's ignore the warnings until
   we have migrated to libsodium */
// TODO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

class WrapKeyHelper {
	AES_KEY buffer;

public:
	AES_KEY *SetEncryptKey(const std::span<const std::byte, 32> key);
	AES_KEY *SetEncryptKey(const CertDatabaseConfig &config,
			       std::string_view name);
	std::pair<const char *, AES_KEY *> SetEncryptKey(const CertDatabaseConfig &config);
	AES_KEY *SetDecryptKey(const std::span<const std::byte, 32> key);
	AES_KEY *SetDecryptKey(const CertDatabaseConfig &config,
			       std::string_view name);
};

#pragma GCC diagnostic pop

AllocatedArray<std::byte>
WrapKey(std::span<const std::byte> src, AES_KEY *wrap_key);

AllocatedArray<std::byte>
UnwrapKey(std::span<const std::byte> src,
	  const CertDatabaseConfig &config, std::string_view key_wrap_name);
