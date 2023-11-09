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

class WrapKey {
	AES_KEY key;

public:
	static WrapKey MakeEncryptKey(const std::span<const std::byte, 32> src);
	static WrapKey MakeEncryptKey(const CertDatabaseConfig &config,
				      std::string_view name);
	static std::pair<const char *, WrapKey> MakeEncryptKey(const CertDatabaseConfig &config);

	static WrapKey MakeDecryptKey(const std::span<const std::byte, 32> src);
	static WrapKey MakeDecryptKey(const CertDatabaseConfig &config,
				      std::string_view name);

	AllocatedArray<std::byte> Encrypt(std::span<const std::byte> src);
	AllocatedArray<std::byte> Decrypt(std::span<const std::byte> src);
};

#pragma GCC diagnostic pop
