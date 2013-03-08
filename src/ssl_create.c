/*
 * SSL/TLS configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_create.h"
#include "ssl_config.h"

#include <inline/compiler.h>

#include <openssl/err.h>
#include <assert.h>
#include <stdbool.h>

static int
verify_callback(int ok, gcc_unused X509_STORE_CTX *ctx)
{
    return ok;
}

static bool
apply_config(SSL_CTX *ssl_ctx, const struct ssl_config *config,
             GError **error_r)
{
    ERR_clear_error();

    if (SSL_CTX_use_RSAPrivateKey_file(ssl_ctx, config->key_file,
                                       SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        g_set_error(error_r, ssl_quark(), 0,
                    "Failed to load key file %s", config->key_file);
        return false;
    }

    if (SSL_CTX_use_certificate_chain_file(ssl_ctx, config->cert_file) != 1) {
        ERR_print_errors_fp(stderr);
        g_set_error(error_r, ssl_quark(), 0,
                    "Failed to load certificate file %s", config->cert_file);
        return false;
    }

    if (config->ca_cert_file != NULL) {
        if (SSL_CTX_load_verify_locations(ssl_ctx, config->ca_cert_file,
                                          NULL) != 1) {
            g_set_error(error_r, ssl_quark(), 0,
                        "Failed to load CA certificate file %s",
                        config->ca_cert_file);
            return false;
        }

        /* send all certificates from this file to the client (list of
           acceptable CA certificates) */

        STACK_OF(X509_NAME) *list =
            SSL_load_client_CA_file(config->ca_cert_file);
        if (list == NULL) {
            g_set_error(error_r, ssl_quark(), 0,
                        "Failed to load CA certificate list from file %s",
                        config->ca_cert_file);
            return false;
        }

        SSL_CTX_set_client_CA_list(ssl_ctx, list);
    }

    if (config->verify != SSL_VERIFY_NO) {
        /* enable client certificates */
        int mode = SSL_VERIFY_PEER;

        if (config->verify == SSL_VERIFY_YES)
            mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

        SSL_CTX_set_verify(ssl_ctx, mode, verify_callback);
    }

    return true;
}

SSL_CTX *
ssl_create(const struct ssl_config *config, GError **error_r)
{
    assert(config != NULL);
    assert(config->cert_file != NULL);
    assert(config->key_file != NULL);

    SSL_CTX *ssl_ctx = SSL_CTX_new(SSLv23_server_method());
    if (ssl_ctx == NULL) {
        g_set_error(error_r, ssl_quark(), 0, "SSL_CTX_new() failed");
        return NULL;
    }

    if (!apply_config(ssl_ctx, config, error_r)) {
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    return ssl_ctx;
}
