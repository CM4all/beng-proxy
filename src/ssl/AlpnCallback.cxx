/*
 * Copyright 2007-2022 CM4all GmbH
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
