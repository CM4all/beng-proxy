/*
 * SSL/TLS configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_factory.hxx"
#include "ssl_config.hxx"
#include "pool.h"

#include <inline/compiler.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <assert.h>
#include <stdbool.h>

struct ssl_cert_key {
    struct ssl_cert_key *next;

    X509 *cert;
    EVP_PKEY *key;
};

struct ssl_factory {
    SSL_CTX *ssl_ctx;

    struct ssl_cert_key cert_key;
};

static int
verify_callback(int ok, gcc_unused X509_STORE_CTX *ctx)
{
    return ok;
}

static void
free_cert_key(struct ssl_cert_key *ck)
{
    X509_free(ck->cert);
    EVP_PKEY_free(ck->key);
}

static void
free_cert_key_list(struct ssl_cert_key *ck)
{
    assert(ck != NULL);

    do {
        free_cert_key(ck);
        ck = ck->next;
    } while (ck != NULL);
}

static BIO *
bio_open_file(const char *path, GError **error_r)
{
    BIO *bio = BIO_new_file(path, "r");
    if (bio == NULL)
        g_set_error(error_r, ssl_quark(), 0,
                    "Failed to open file %s", path);

    return bio;
}

static EVP_PKEY *
read_key_file(const char *path, GError **error_r)
{
    BIO *bio = bio_open_file(path, error_r);
    if (bio == NULL)
        return NULL;

    EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (key == NULL)
        g_set_error(error_r, ssl_quark(), 0,
                    "Failed to load key file %s", path);

    return key;
}

static X509 *
read_cert_file(const char *path, GError **error_r)
{
    BIO *bio = bio_open_file(path, error_r);
    if (bio == NULL)
        return NULL;

    X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (cert == NULL)
        g_set_error(error_r, ssl_quark(), 0,
                    "Failed to load certificate file %s", path);

    return cert;
}

static bool
load_cert_key(struct ssl_cert_key *ck,
              const struct ssl_cert_key_config *config,
              GError **error_r)
{
    ck->key = read_key_file(config->key_file, error_r);
    if (ck->key == NULL)
        return false;

    ck->cert = read_cert_file(config->cert_file, error_r);
    if (ck->cert == NULL) {
        EVP_PKEY_free(ck->key);
        return false;
    }

    if (X509_verify(ck->cert, ck->key) != 0) {
        free_cert_key(ck);
        g_set_error(error_r, ssl_quark(), 0,
                    "Key '%s' does not match certificate '%s'",
                    config->key_file, config->cert_file);
        return false;
    }

    return true;
}

static bool
load_certs_keys(struct pool *pool, struct ssl_factory *factory,
                const struct ssl_config *config,
                GError **error_r)
{
    assert(factory != NULL);
    assert(config != NULL);

    const struct ssl_cert_key_config *c = &config->cert_key;

    if (!load_cert_key(&factory->cert_key, c, error_r))
        return false;

    struct ssl_cert_key **ck_tail = &factory->cert_key.next;
    *ck_tail = NULL;

    for (c = c->next; c != NULL; c = c->next) {
        struct ssl_cert_key *ck =
            (struct ssl_cert_key *)p_malloc(pool, sizeof(*ck));

        if (!load_cert_key(ck, c, error_r)) {
            free_cert_key_list(&factory->cert_key);
            return false;
        }

        *ck_tail = ck;
        ck_tail = &ck->next;
        ck->next = NULL;
    }

    return true;
}

static bool
apply_config(SSL_CTX *ssl_ctx, const struct ssl_config *config,
             GError **error_r)
{
    ERR_clear_error();

    if (SSL_CTX_use_RSAPrivateKey_file(ssl_ctx, config->cert_key.key_file,
                                       SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        g_set_error(error_r, ssl_quark(), 0,
                    "Failed to load key file %s", config->cert_key.key_file);
        return false;
    }

    if (SSL_CTX_use_certificate_chain_file(ssl_ctx, config->cert_key.cert_file) != 1) {
        ERR_print_errors_fp(stderr);
        g_set_error(error_r, ssl_quark(), 0,
                    "Failed to load certificate file %s", config->cert_key.cert_file);
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

static bool
match_cn(X509_NAME *subject, const char *host_name, size_t hn_length)
{
    char common_name[256];
    if (X509_NAME_get_text_by_NID(subject, NID_commonName, common_name,
                                  sizeof(common_name)) < 0)
        return false;

    if (strcmp(host_name, common_name) == 0)
        return true;

    if (common_name[0] == '*' && common_name[1] == '.' &&
        common_name[2] != 0) {
        const size_t cn_length = strlen(common_name);
        if (hn_length > cn_length &&
            memcmp(host_name + hn_length - cn_length + 1,
                   common_name + 1, cn_length - 1) == 0)
            return true;
    }

    return false;
}

static bool
use_cert_key(SSL *ssl, const struct ssl_cert_key *ck)
{
    return SSL_use_certificate(ssl, ck->cert) == 1 &&
        SSL_use_PrivateKey(ssl, ck->key) == 1;
}

static int
ssl_servername_callback(SSL *ssl, gcc_unused int *al,
                        struct ssl_factory *factory)
{
    const char *host_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (host_name == NULL)
        return SSL_TLSEXT_ERR_OK;

    const size_t length = strlen(host_name);

    /* find the first certificate that matches */

    const struct ssl_cert_key *ck = &factory->cert_key;
    do {
        X509_NAME *subject = X509_get_subject_name(ck->cert);
        if (subject != NULL && match_cn(subject, host_name, length)) {
            /* found it - now use it */
            use_cert_key(ssl, ck);
            break;
        }

        ck = ck->next;
    } while (ck != NULL);

    return SSL_TLSEXT_ERR_NOACK;
}

static bool
ssl_factory_enable_sni(struct ssl_factory *factory, GError **error_r)
{
    if (!SSL_CTX_set_tlsext_servername_callback(factory->ssl_ctx,
                                                ssl_servername_callback) ||
        !SSL_CTX_set_tlsext_servername_arg(factory->ssl_ctx, factory)) {
        g_set_error(error_r, ssl_quark(), 0,
                    "SSL_CTX_set_tlsext_servername_callback() failed");
        return false;
    }

    return true;
}

struct ssl_factory *
ssl_factory_new(struct pool *pool, const struct ssl_config *config,
                GError **error_r)
{
    assert(pool != NULL);
    assert(config != NULL);
    assert(config->cert_key.cert_file != NULL);
    assert(config->cert_key.key_file != NULL);

    struct ssl_factory *factory =
        (struct ssl_factory *)p_malloc(pool, sizeof(*factory));
    SSL_CTX *ssl_ctx = factory->ssl_ctx = SSL_CTX_new(SSLv23_server_method());
    if (ssl_ctx == NULL) {
        g_set_error(error_r, ssl_quark(), 0, "SSL_CTX_new() failed");
        return NULL;
    }

    if (!apply_config(ssl_ctx, config, error_r) ||
        !load_certs_keys(pool, factory, config, error_r)) {
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    if (factory->cert_key.next != NULL &&
        !ssl_factory_enable_sni(factory, error_r)) {
        free_cert_key_list(&factory->cert_key);
        SSL_CTX_free(factory->ssl_ctx);
        return NULL;
    }

    return factory;
}

void
ssl_factory_free(struct ssl_factory *factory)
{
    free_cert_key_list(&factory->cert_key);
    SSL_CTX_free(factory->ssl_ctx);
}

SSL *
ssl_factory_make(struct ssl_factory *factory)
{
    return SSL_new(factory->ssl_ctx);
}
