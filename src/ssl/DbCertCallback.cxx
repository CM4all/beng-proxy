// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
DbSslCertCallback::OnCertCallback(SSL &ssl, const char *name) noexcept
{
	const char *special = nullptr;

	if (const auto alpn_selected = GetAlpnSelected(ssl);
	    Equals(alpn_selected, std::span{alpn_acme_tls1}.subspan(1)))
		special = "acme-alpn-tls-01";

	return cache.Apply(ssl, name, special);
}
