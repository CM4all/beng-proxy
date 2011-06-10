/*
 * SSL/TLS configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_create.h"
#include "ssl_config.h"

#include <openssl/err.h>
#include <assert.h>
#include <stdbool.h>

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

    if (SSL_CTX_use_certificate_file(ssl_ctx, config->cert_file,
                                     SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        g_set_error(error_r, ssl_quark(), 0,
                    "Failed to load certificate file %s", config->key_file);
        return false;
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
