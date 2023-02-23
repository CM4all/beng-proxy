// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <openssl/ossl_typ.h>

#include <span>
#include <string>

class AlpnCallback {
	using Span = std::span<const unsigned char>;

	std::string advertised;

public:
	void Add(Span p) noexcept {
		advertised.append((const char *)p.data(), p.size());
	}

	void Setup(SSL_CTX &ssl_ctx) noexcept;

private:
	Span NextProtosAdvertisedCallback(SSL &ssl) noexcept;
	static int NextProtosAdvertisedCallback(SSL *ssl,
						const unsigned char **data,
						unsigned int *len,
						void *ctx) noexcept;

	Span SelectCallback(SSL &ssl, Span in) noexcept;
	static int SelectCallback(SSL *ssl, const unsigned char **out,
				  unsigned char *outlen,
				  const unsigned char *in, unsigned int inlen,
				  void *ctx) noexcept;
};
