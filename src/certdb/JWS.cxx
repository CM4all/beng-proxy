// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "JWS.hxx"
#include "lib/sodium/Base64.hxx"
#include "lib/openssl/Buffer.hxx"
#include "util/AllocatedString.hxx"

#include "openssl/evp.h"
#include "openssl/rsa.h"

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include "util/ScopeExit.hxx"
#include <openssl/core_names.h>
#endif

#include <boost/json.hpp>

#include <stdexcept>

boost::json::object
MakeJwk(EVP_PKEY &key)
{
	if (EVP_PKEY_base_id(&key) != EVP_PKEY_RSA)
		throw std::runtime_error("RSA key expected");

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	BIGNUM *n = nullptr;
	if (!EVP_PKEY_get_bn_param(&key, OSSL_PKEY_PARAM_RSA_N, &n))
		throw std::runtime_error("Failed to get RSA N value");

	AtScopeExit(n) { BN_clear_free(n); };

	BIGNUM *e = nullptr;
	if (!EVP_PKEY_get_bn_param(&key, OSSL_PKEY_PARAM_RSA_E, &e))
		throw std::runtime_error("Failed to get RSA E value");

	AtScopeExit(e) { BN_clear_free(e); };
#else
	const BIGNUM *n, *e;
	RSA_get0_key(EVP_PKEY_get0_RSA(&key), &n, &e, nullptr);
#endif

	boost::json::object root;
	root.emplace("e", UrlSafeBase64(SslBuffer(*e).get()).c_str());
	root.emplace("kty", "RSA");
	root.emplace("n", UrlSafeBase64(SslBuffer(*n).get()).c_str());
	return root;
}
