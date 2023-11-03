// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AcmeKey.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/openssl/LoadFile.hxx"

AcmeKey::AcmeKey(const char *path)
	:key(LoadKeyFile(path))
{
	if (EVP_PKEY_get_base_id(key.get()) != EVP_PKEY_RSA &&
	    EVP_PKEY_get_base_id(key.get()) != EVP_PKEY_EC)
		throw FmtRuntimeError("File '{}' does not contain an EC or RSA key",
				      path);
}
