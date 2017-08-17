/*
 * Copyright 2007-2017 Content Management AG
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
#include "ssl_config.hxx"
#include "ssl/Error.hxx"
#include "ssl/Ctx.hxx"
#include "ssl/Unique.hxx"

#include "util/Compiler.h"

#include <openssl/err.h>

/**
 * Enable Elliptic curve Diffie-Hellman (ECDH) for perfect forward
 * secrecy.  By default, it OpenSSL disables it.
 */
static void
enable_ecdh(SSL_CTX &ssl_ctx)
{
    /* OpenSSL 1.0.2 will allow this instead:

       SSL_CTX_set_ecdh_auto(ssl_ctx, 1)
    */

    UniqueEC_KEY ecdh(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
    if (ecdh == nullptr)
        throw SslError("EC_KEY_new_by_curve_name() failed");

    if (SSL_CTX_set_tmp_ecdh(&ssl_ctx, ecdh.get()) != 1)
        throw SslError("SSL_CTX_set_tmp_ecdh() failed");
}

static void
SetupBasicSslCtx(SSL_CTX &ssl_ctx, bool server)
{
    long mode = SSL_MODE_ENABLE_PARTIAL_WRITE
        | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER;
#ifdef SSL_MODE_RELEASE_BUFFERS
    /* requires libssl 1.0.0 */
    mode |= SSL_MODE_RELEASE_BUFFERS;
#endif

    /* without this flag, OpenSSL attempts to verify the whole local
       certificate chain for each connection, which is a waste of CPU
       time */
    mode |= SSL_MODE_NO_AUTO_CHAIN;

    SSL_CTX_set_mode(&ssl_ctx, mode);

    if (server) {
        enable_ecdh(ssl_ctx);

        /* no auto-clear, because LbInstance::compress_event will do
           this every 10 minutes, which is more reliable */
        SSL_CTX_set_session_cache_mode(&ssl_ctx,
                                       SSL_SESS_CACHE_SERVER|
                                       SSL_SESS_CACHE_NO_AUTO_CLEAR);
    }

    /* disable protocols that are known to be insecure */
    SSL_CTX_set_options(&ssl_ctx, SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);

    /* disable weak ciphers */
    SSL_CTX_set_cipher_list(&ssl_ctx, "DEFAULT:!EXPORT:!LOW:!RC4");

    /* let us choose the cipher based on our own priority; so if a
       client prefers to use a weak cipher (which would be rather
       stupid, but oh well..), choose the strongest one supported by
       the client; this call is only here to maximize our SSL/TLS
       "score" in benchmarks which think following the client's
       preferences is bad */
    SSL_CTX_set_options(&ssl_ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
}

SslCtx
CreateBasicSslCtx(bool server)
{
    ERR_clear_error();

    /* don't be fooled - we want TLS, not SSL - but TLSv1_method()
       will only allow TLSv1.0 and will refuse TLSv1.1 and TLSv1.2;
       only SSLv23_method() supports all (future) TLS protocol
       versions, even if we don't want any SSL at all */
    auto method = server
        ? SSLv23_server_method()
        : SSLv23_client_method();

    SslCtx ssl_ctx(method);
    SetupBasicSslCtx(*ssl_ctx, server);
    return ssl_ctx;
}

static int
verify_callback(int ok, gcc_unused X509_STORE_CTX *ctx)
{
    return ok;
}

void
ApplyServerConfig(SSL_CTX &ssl_ctx, const SslConfig &config)
{
    ERR_clear_error();

    if (!config.ca_cert_file.empty()) {
        if (SSL_CTX_load_verify_locations(&ssl_ctx,
                                          config.ca_cert_file.c_str(),
                                          nullptr) != 1)
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
