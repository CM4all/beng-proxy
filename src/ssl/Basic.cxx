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

#include "Basic.hxx"
#include "Config.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/Ctx.hxx"

#include <openssl/err.h>
#include <openssl/opensslv.h>

#include <stdio.h>

static void
keylog(const SSL *, const char *line)
{
	const char *path = getenv("SSLKEYLOGFILE");
	if (path == nullptr)
		return;

	FILE *file = fopen(path, "a");
	if (file != nullptr) {
		  fprintf(file, "%s\n", line);
		  fclose(file);
	}
}

static void
SetupBasicSslCtx(SSL_CTX &ssl_ctx, bool server)
{
	long mode = SSL_MODE_ENABLE_PARTIAL_WRITE
		| SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER
		| SSL_MODE_RELEASE_BUFFERS;

	/* without this flag, OpenSSL attempts to verify the whole local
	   certificate chain for each connection, which is a waste of CPU
	   time */
	mode |= SSL_MODE_NO_AUTO_CHAIN;

	SSL_CTX_set_mode(&ssl_ctx, mode);

	if (server) {
		/* no auto-clear, because LbInstance::compress_event will do
		   this every 10 minutes, which is more reliable */
		SSL_CTX_set_session_cache_mode(&ssl_ctx,
					       SSL_SESS_CACHE_SERVER|
					       SSL_SESS_CACHE_NO_AUTO_CLEAR);
	}

	/* disable protocols that are known to be insecure */
	SSL_CTX_set_min_proto_version(&ssl_ctx, TLS1_2_VERSION);

	/* disable weak ciphers */
	/* "!SHA1:!SHA256:!SHA384" disables insecure CBC ciphers */
	SSL_CTX_set_cipher_list(&ssl_ctx, "DEFAULT:!EXPORT:!LOW:!RC4:!SHA1:!SHA256:!SHA384");

	/* let us choose the cipher based on our own priority; so if a
	   client prefers to use a weak cipher (which would be rather
	   stupid, but oh well..), choose the strongest one supported by
	   the client; this call is only here to maximize our SSL/TLS
	   "score" in benchmarks which think following the client's
	   preferences is bad */
	SSL_CTX_set_options(&ssl_ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);

	/* support logging session secrets for Wireshark */
	if (getenv("SSLKEYLOGFILE") != nullptr)
		SSL_CTX_set_keylog_callback(&ssl_ctx, keylog);
}

SslCtx
CreateBasicSslCtx(bool server)
{
	ERR_clear_error();

	auto method = server
		? TLS_server_method()
		: TLS_client_method();

	SslCtx ssl_ctx(method);
	SetupBasicSslCtx(*ssl_ctx, server);
	return ssl_ctx;
}

static int
verify_callback(int ok, X509_STORE_CTX *) noexcept
{
	return ok;
}

void
ApplyServerConfig(SSL_CTX &ssl_ctx, const SslConfig &config)
{
	ERR_clear_error();

	if (!config.ca_cert_file.empty()) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
		if (SSL_CTX_load_verify_file(&ssl_ctx,
					     config.ca_cert_file.c_str()) != 1)
			throw SslError("Failed to load CA certificate file " +
				       config.ca_cert_file);
#else
		if (SSL_CTX_load_verify_locations(&ssl_ctx,
						  config.ca_cert_file.c_str(),
						  nullptr) != 1)
			throw SslError("Failed to load CA certificate file " +
				       config.ca_cert_file);
#endif

		/* send all certificates from this file to the client (list of
		   acceptable CA certificates) */

		STACK_OF(X509_NAME) *list =
			SSL_load_client_CA_file(config.ca_cert_file.c_str());
		if (list == nullptr)
			throw SslError("Failed to load CA certificate list from file " +
				       config.ca_cert_file);

		SSL_CTX_set_client_CA_list(&ssl_ctx, list);
	}

	if (config.verify != SslVerify::NO) {
		/* enable client certificates */
		int mode = SSL_VERIFY_PEER;

		if (config.verify == SslVerify::YES)
			mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

		SSL_CTX_set_verify(&ssl_ctx, mode, verify_callback);
	}
}
