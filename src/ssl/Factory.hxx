// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "AlpnCallback.hxx"
#include "lib/openssl/UniqueSSL.hxx"
#include "lib/openssl/Ctx.hxx"

#include <memory>
#include <span>
#include <string_view>
#include <vector>

struct pool;
struct SslConfig;
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
	const SslFactoryCertKey *FindCommonName(std::string_view host_name) const noexcept;

	/**
	 * Wrapper for SSL_CTX_set_session_id_context().
	 *
	 * Throws on error.
	 */
	void SetSessionIdContext(std::span<const std::byte> sid_ctx);

	UniqueSSL Make();

private:
	int CertCallback(SSL &ssl) noexcept;
	static int CertCallback(SSL *ssl, void *arg) noexcept;
};
