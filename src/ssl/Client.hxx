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

/*
 * Glue code for using the ssl_filter in a client connection.
 */

#pragma once

#include "AlpnClient.hxx"
#include "ssl/Ctx.hxx"
#include "fs/Ptr.hxx"
#include "util/Compiler.h"

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
	gcc_const
	static SslClientFactory &GetFactory(SSL_CTX *ssl_ctx) noexcept {
		return *(SslClientFactory *)SSL_CTX_get_ex_data(ssl_ctx, idx);
	}

	gcc_const
	static SslClientFactory &GetFactory(SSL *ssl) noexcept {
		return GetFactory(SSL_get_SSL_CTX(ssl));
	}

	int ClientCertCallback_(SSL *ssl, X509 **x509,
				EVP_PKEY **pkey) noexcept;
	static int ClientCertCallback(SSL *ssl, X509 **x509,
				      EVP_PKEY **pkey) noexcept;
};
