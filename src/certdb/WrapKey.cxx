/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "WrapKey.hxx"

#include <openssl/err.h>

#include <algorithm>

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
/* the AES_wrap_key() API was deprecated in OpenSSL 3.0.0, but its
   replacement is more complicated, so let's ignore the warnings until
   we have migrated to libsodium */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

Pg::BinaryValue
WapKey(Pg::BinaryValue key_der, AES_KEY *wrap_key,
       std::unique_ptr<unsigned char[]> &wrapped)
{
	std::unique_ptr<unsigned char[]> padded;
	size_t padded_size = ((key_der.size - 1) | 7) + 1;
	if (padded_size != key_der.size) {
		/* pad with zeroes */
		padded.reset(new unsigned char[padded_size]);

		unsigned char *p = padded.get();
		p = std::copy_n((const unsigned char *)key_der.data,
				key_der.size, p);
		std::fill(p, padded.get() + padded_size, 0);

		key_der.data = padded.get();
		key_der.size = padded_size;
	}

	wrapped.reset(new unsigned char[key_der.size + 8]);
	int result = AES_wrap_key(wrap_key, nullptr,
				  wrapped.get(),
				  (const unsigned char *)key_der.data,
				  key_der.size);
	if (result <= 0)
		throw SslError("AES_wrap_key() failed");

	key_der.data = wrapped.get();
	key_der.size = result;
	return key_der;
}

Pg::BinaryValue
UnwrapKey(Pg::BinaryValue key_der,
	  const CertDatabaseConfig &config, const std::string &key_wrap_name,
	  std::unique_ptr<unsigned char[]> &unwrapped)
{
	if (key_der.size <= 8)
		throw std::runtime_error("Malformed wrapped key");

	auto i = config.wrap_keys.find(key_wrap_name);
	if (i == config.wrap_keys.end())
		throw std::runtime_error(std::string("No such wrap_key: ") +
					 key_wrap_name);

	WrapKeyHelper wrap_key_helper;
	const auto wrap_key =
		wrap_key_helper.SetDecryptKey(config, key_wrap_name);

	ERR_clear_error();

	unwrapped.reset(new unsigned char[key_der.size - 8]);
	int r = AES_unwrap_key(wrap_key, nullptr,
			       unwrapped.get(),
			       (const unsigned char *)key_der.data,
			       key_der.size);
	if (r <= 0)
		throw SslError("AES_unwrap_key() failed");

	key_der.data = unwrapped.get();
	key_der.size = r;
	return key_der;
}
