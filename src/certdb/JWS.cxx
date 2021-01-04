/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "JWS.hxx"
#include "sodium/Base64.hxx"
#include "ssl/Buffer.hxx"
#include "util/AllocatedString.hxx"

#include "openssl/evp.h"
#include "openssl/rsa.h"

#include <boost/json.hpp>

#include <stdexcept>

boost::json::object
MakeJwk(EVP_PKEY &key)
{
	if (EVP_PKEY_base_id(&key) != EVP_PKEY_RSA)
		throw std::runtime_error("RSA key expected");

	const BIGNUM *n, *e;
	RSA_get0_key(EVP_PKEY_get0_RSA(&key), &n, &e, nullptr);

	boost::json::object root;
	root.emplace("e", UrlSafeBase64(SslBuffer(*e).get()).c_str());
	root.emplace("kty", "RSA");
	root.emplace("n", UrlSafeBase64(SslBuffer(*n).get()).c_str());
	return root;
}
