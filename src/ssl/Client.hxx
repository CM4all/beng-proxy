// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Glue code for using the ssl_filter in a client connection.
 */

#pragma once

#include "AlpnClient.hxx"
#include "lib/openssl/Ctx.hxx"
#include "fs/Ptr.hxx"

#include <memory>

struct SslClientConfig;
class EventLoop;
class SslClientCerts;

class SslClientFactory {
	SslCtx ctx;
	std::unique_ptr<SslClientCerts> certs;

	static inline int idx = -1;

public:
	explicit SslClientFactory(const SslClientConfig &config);
	~SslClientFactory() noexcept;

	/**
	 * Throws on error.
	 *
	 * @param certificate the name of the client certificate to be
	 * used
	 */
	SocketFilterPtr Create(EventLoop &event_loop,
			       const char *hostname,
			       const char *certificate,
			       SslClientAlpn alpn=SslClientAlpn::NONE);

private:
	[[gnu::const]]
	static SslClientFactory &GetFactory(SSL_CTX *ssl_ctx) noexcept {
		return *(SslClientFactory *)SSL_CTX_get_ex_data(ssl_ctx, idx);
	}

	[[gnu::const]]
	static SslClientFactory &GetFactory(SSL *ssl) noexcept {
		return GetFactory(SSL_get_SSL_CTX(ssl));
	}

	int ClientCertCallback_(SSL *ssl, X509 **x509,
				EVP_PKEY **pkey) noexcept;
	static int ClientCertCallback(SSL *ssl, X509 **x509,
				      EVP_PKEY **pkey) noexcept;
};
