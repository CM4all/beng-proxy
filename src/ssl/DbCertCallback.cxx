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

#include "DbCertCallback.hxx"
#include "Cache.hxx"
#include "AlpnProtos.hxx"

#include <openssl/ssl.h>

#include <cstring>
#include <span>

[[gnu::pure]]
static std::span<const unsigned char>
GetAlpnSelected(SSL &ssl) noexcept
{
	const unsigned char *data;
	unsigned int length;
	SSL_get0_alpn_selected(&ssl, &data, &length);
	return {data, length};
}

[[gnu::pure]]
static bool
Equals(std::span<const unsigned char> a,
       std::span<const unsigned char> b) noexcept
{
	return a.size() == b.size() &&
		memcmp(a.data(), b.data(), a.size()) == 0;
}

LookupCertResult
DbSslCertCallback::OnCertCallback(SSL &ssl, const char *name)
{
	const char *special = nullptr;

	if (const auto alpn_selected = GetAlpnSelected(ssl);
	    Equals(alpn_selected, std::span{alpn_acme_tls1}.subspan(1)))
		special = "acme-alpn-tls-01";

	return cache.Apply(ssl, name, special);
}
