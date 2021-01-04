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

#include "AlpnEnable.hxx"
#include "AlpnProtos.hxx"
#include "AlpnSelect.hxx"
#include "util/ConstBuffer.hxx"

#include <openssl/ssl.h>

#include <iterator> // for std::size()

void
EnableAlpnH2(SSL_CTX &ssl_ctx) noexcept
{
	SSL_CTX_set_next_protos_advertised_cb(&ssl_ctx, [](SSL *, const unsigned char **data, unsigned int *len, void *) {
		*data = alpn_http_any;
		*len = std::size(alpn_http_any);
		return SSL_TLSEXT_ERR_OK;
	}, nullptr);

	SSL_CTX_set_alpn_select_cb(&ssl_ctx, [](SSL *, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *){
		ConstBuffer<unsigned char> haystack(in, inlen);
		auto found = FindAlpn(haystack, alpn_h2);
		if (found.IsNull())
			found = FindAlpn(haystack, alpn_http_1_1);
		if (found.IsNull())
			return SSL_TLSEXT_ERR_NOACK;

		*out = found.data;
		*outlen = found.size;
		return SSL_TLSEXT_ERR_OK;
	}, nullptr);
}

