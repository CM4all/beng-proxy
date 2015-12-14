/*
 * SSL/TLS initialisation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Basic.hxx"
#include "Error.hxx"
#include "ssl_config.hxx"

#include <inline/compiler.h>

#include <openssl/err.h>

/**
 * Enable Elliptic curve Diffie-Hellman (ECDH) for perfect forward
 * secrecy.  By default, it OpenSSL disables it.
 */
static void
enable_ecdh(SSL_CTX *ssl_ctx)
{
    /* OpenSSL 1.0.2 will allow this instead:

       SSL_CTX_set_ecdh_auto(ssl_ctx, 1)
    */

    UniqueEC_KEY ecdh(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
    if (ecdh == nullptr)
        throw SslError("EC_KEY_new_by_curve_name() failed");

    if (SSL_CTX_set_tmp_ecdh(ssl_ctx, ecdh.get()) != 1)
        throw SslError("SSL_CTX_set_tmp_ecdh() failed");
}

static void
SetupBasicSslCtx(SSL_CTX *ssl_ctx, bool server)
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

    SSL_CTX_set_mode(ssl_ctx, mode);

    if (server)
        enable_ecdh(ssl_ctx);

    /* disable protocols that are known to be insecure */
    SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);

    /* disable weak ciphers */
    SSL_CTX_set_cipher_list(ssl_ctx, "DEFAULT:!EXPORT:!LOW");
}

UniqueSSL_CTX
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

    UniqueSSL_CTX ssl_ctx(SSL_CTX_new(method));
    if (ssl_ctx == nullptr)
        throw SslError("SSL_CTX_new() failed");

    SetupBasicSslCtx(ssl_ctx.get(), server);
    return ssl_ctx;
}

static int
verify_callback(int ok, gcc_unused X509_STORE_CTX *ctx)
{
    return ok;
}

void
ApplyServerConfig(SSL_CTX *ssl_ctx, const SslConfig &config)
{
    ERR_clear_error();

    if (!config.ca_cert_file.empty()) {
        if (SSL_CTX_load_verify_locations(ssl_ctx,
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

        SSL_CTX_set_client_CA_list(ssl_ctx, list);
    }

    if (config.verify != SslVerify::NO) {
        /* enable client certificates */
        int mode = SSL_VERIFY_PEER;

        if (config.verify == SslVerify::YES)
            mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

        SSL_CTX_set_verify(ssl_ctx, mode, verify_callback);
    }
}
