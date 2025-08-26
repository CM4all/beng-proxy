// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
		/* disable session resumption for now (still
		   experimenting with performance tweaks) */
		SSL_CTX_set_session_cache_mode(&ssl_ctx, SSL_SESS_CACHE_OFF);
		SSL_CTX_set_num_tickets(&ssl_ctx, 0);

		// TODO remove the following code (and the timer)?
#if 0
		/* no auto-clear, because LbInstance::compress_event will do
		   this every 10 minutes, which is more reliable */
		SSL_CTX_set_session_cache_mode(&ssl_ctx,
					       SSL_SESS_CACHE_SERVER|
					       SSL_SESS_CACHE_NO_AUTO_CLEAR);
#endif
	}

	/* disable protocols that are known to be insecure */
	SSL_CTX_set_min_proto_version(&ssl_ctx, TLS1_3_VERSION);

	/* disable weak ciphers */
	SSL_CTX_set_cipher_list(&ssl_ctx, "DEFAULT"
				":!EXPORT:!LOW:!MEDIUM"
				":!RC4"
				/* disable weak AES ciphers */
				":!AES128"
				/* disable insecure CBC ciphers */
				":!SHA1:!SHA256:!SHA384");

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
		if (SSL_CTX_load_verify_file(&ssl_ctx,
					     config.ca_cert_file.c_str()) != 1)
			throw SslError("Failed to load CA certificate file " +
				       config.ca_cert_file);

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
