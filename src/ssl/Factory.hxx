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

#pragma once

#include "AlpnCallback.hxx"
#include "lib/openssl/UniqueSSL.hxx"
#include "lib/openssl/Ctx.hxx"

#include <memory>
#include <vector>

struct StringView;
struct pool;
struct SslConfig;
template<typename T> struct ConstBuffer;
struct SslFactoryCertKey;
class SslCertCallback;

class SslFactory {
	AlpnCallback alpn_callback;

	SslCtx ssl_ctx;

	std::vector<SslFactoryCertKey> cert_key;

	const std::unique_ptr<SslCertCallback> cert_callback;

public:
	SslFactory(const SslConfig &config,
		   std::unique_ptr<SslCertCallback> _cert_callback);
	~SslFactory() noexcept;

	void AddAlpn(std::span<const unsigned char> p) {
		alpn_callback.Add(p);
		alpn_callback.Setup(*ssl_ctx);
	}

	[[gnu::pure]]
	const SslFactoryCertKey *FindCommonName(StringView host_name) const noexcept;

	/**
	 * Wrapper for SSL_CTX_set_session_id_context().
	 *
	 * Throws on error.
	 */
	void SetSessionIdContext(ConstBuffer<void> sid_ctx);

	UniqueSSL Make();

private:
	int CertCallback(SSL &ssl) noexcept;
	static int CertCallback(SSL *ssl, void *arg) noexcept;
};
