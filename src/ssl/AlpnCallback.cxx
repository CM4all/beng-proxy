// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AlpnCallback.hxx"
#include "AlpnIterator.hxx"
#include "AlpnSelect.hxx"

#include <openssl/ssl.h>

#include <cassert>

inline AlpnCallback::Span
AlpnCallback::NextProtosAdvertisedCallback(SSL &) noexcept
{
	return {(const unsigned char *)advertised.data(), advertised.size()};
}

int
AlpnCallback::NextProtosAdvertisedCallback(SSL *ssl,
					   const unsigned char **data,
					   unsigned int *len,
					   void *ctx) noexcept
{
	auto &c = *(AlpnCallback *)ctx;
	auto a = c.NextProtosAdvertisedCallback(*ssl);
	assert(!a.empty());

	*data = a.data();
	*len = a.size();
	return SSL_TLSEXT_ERR_OK;
}

AlpnCallback::Span
AlpnCallback::SelectCallback(SSL &ssl, Span in) noexcept
{
	for (const auto a : AlpnRange{NextProtosAdvertisedCallback(ssl)}) {
		if (auto found = FindAlpn(in, a); !found.empty())
			return found;
	}

	return {};
}

int
AlpnCallback::SelectCallback(SSL *ssl, const unsigned char **out,
			     unsigned char *outlen,
			     const unsigned char *in, unsigned int inlen,
			     void *ctx) noexcept
{
	auto &c = *(AlpnCallback *)ctx;
	auto s = c.SelectCallback(*ssl, {in, inlen});
	if (s.empty())
		return SSL_TLSEXT_ERR_NOACK;

	*out = s.data();
	*outlen = s.size();
	return SSL_TLSEXT_ERR_OK;
}

void
AlpnCallback::Setup(SSL_CTX &ssl_ctx) noexcept
{
	if (advertised.empty())
		return;

	SSL_CTX_set_next_protos_advertised_cb(&ssl_ctx,
					      NextProtosAdvertisedCallback,
					      this);
	SSL_CTX_set_alpn_select_cb(&ssl_ctx, SelectCallback, this);
}
