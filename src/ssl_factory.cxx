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
    X509 *cert;
    EVP_PKEY *key;

    ssl_cert_key():cert(nullptr), key(nullptr) {}

    ssl_cert_key(X509 *_cert, EVP_PKEY *_key)
        :cert(_cert), key(_key) {}

    ssl_cert_key(ssl_cert_key &&other)
        :cert(other.cert), key(other.key) {
        other.cert = nullptr;
        other.key = nullptr;
    }

    ~ssl_cert_key() {
        if (cert != nullptr)
            X509_free(cert);
        if (key != nullptr)
            EVP_PKEY_free(key);
    }

    bool Load(const ssl_cert_key_config &config, GError **error_r);
};

struct ssl_factory {
    SSL_CTX *const ssl_ctx;

    std::vector<ssl_cert_key> cert_key;

    ssl_factory(SSL_CTX *_ssl_ctx):ssl_ctx(_ssl_ctx) {}

    ~ssl_factory() {
        SSL_CTX_free(ssl_ctx);
    }

    bool EnableSNI(GError **error_r);
};

static int
verify_callback(int ok, gcc_unused X509_STORE_CTX *ctx)
{
    return ok;
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

bool
ssl_cert_key::Load(const ssl_cert_key_config &config, GError **error_r)
{
    assert(key == nullptr);
    assert(cert == nullptr);

    key = read_key_file(config.key_file.c_str(), error_r);
    if (key == nullptr)
        return false;

    cert = read_cert_file(config.cert_file.c_str(), error_r);
    if (cert == nullptr)
        return false;

    if (X509_verify(cert, key) != 0) {
        g_set_error(error_r, ssl_quark(), 0,
                    "Key '%s' does not match certificate '%s'",
                    config.key_file.c_str(), config.cert_file.c_str());
        return false;
    }

    return true;
}

static bool
load_certs_keys(ssl_factory &factory, const ssl_config &config,
                GError **error_r)
{
    factory.cert_key.reserve(config.cert_key.size());

    for (const auto &c : config.cert_key) {
        ssl_cert_key ck;
        if (!ck.Load(c, error_r))
            return false;

        factory.cert_key.emplace_back(std::move(ck));
    }

    return true;
}

static bool
apply_config(SSL_CTX *ssl_ctx, const struct ssl_config *config,
             GError **error_r)
{
    assert(!config->cert_key.empty());

    ERR_clear_error();

    if (SSL_CTX_use_RSAPrivateKey_file(ssl_ctx,
                                       config->cert_key[0].key_file.c_str(),
                                       SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        g_set_error(error_r, ssl_quark(), 0,
                    "Failed to load key file %s",
                    config->cert_key[0].key_file.c_str());
        return false;
    }

    if (SSL_CTX_use_certificate_chain_file(ssl_ctx,
                                           config->cert_key[0].cert_file.c_str()) != 1) {
        ERR_print_errors_fp(stderr);
        g_set_error(error_r, ssl_quark(), 0,
                    "Failed to load certificate file %s",
                    config->cert_key[0].cert_file.c_str());
        return false;
    }

    if (!config->ca_cert_file.empty()) {
        if (SSL_CTX_load_verify_locations(ssl_ctx,
                                          config->ca_cert_file.c_str(),
                                          NULL) != 1) {
            g_set_error(error_r, ssl_quark(), 0,
                        "Failed to load CA certificate file %s",
                        config->ca_cert_file.c_str());
            return false;
        }

        /* send all certificates from this file to the client (list of
           acceptable CA certificates) */

        STACK_OF(X509_NAME) *list =
            SSL_load_client_CA_file(config->ca_cert_file.c_str());
        if (list == NULL) {
            g_set_error(error_r, ssl_quark(), 0,
                        "Failed to load CA certificate list from file %s",
                        config->ca_cert_file.c_str());
            return false;
        }

        SSL_CTX_set_client_CA_list(ssl_ctx, list);
    }

    if (config->verify != ssl_verify::NO) {
        /* enable client certificates */
        int mode = SSL_VERIFY_PEER;

        if (config->verify == ssl_verify::YES)
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
use_cert_key(SSL *ssl, const ssl_cert_key &ck)
{
    return SSL_use_certificate(ssl, ck.cert) == 1 &&
        SSL_use_PrivateKey(ssl, ck.key) == 1;
}

static int
ssl_servername_callback(SSL *ssl, gcc_unused int *al,
                        const ssl_factory &factory)
{
    const char *host_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (host_name == NULL)
        return SSL_TLSEXT_ERR_OK;

    const size_t length = strlen(host_name);

    /* find the first certificate that matches */

    for (const auto &ck : factory.cert_key) {
        X509_NAME *subject = X509_get_subject_name(ck.cert);
        if (subject != NULL && match_cn(subject, host_name, length)) {
            /* found it - now use it */
            use_cert_key(ssl, ck);
            break;
        }
    }

    return SSL_TLSEXT_ERR_OK;
}

inline bool
ssl_factory::EnableSNI(GError **error_r)
{
    if (!SSL_CTX_set_tlsext_servername_callback(ssl_ctx,
                                                ssl_servername_callback) ||
        !SSL_CTX_set_tlsext_servername_arg(ssl_ctx, this)) {
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
    assert(!config->cert_key.empty());

    SSL_CTX *ssl_ctx = SSL_CTX_new(SSLv23_server_method());
    if (ssl_ctx == NULL) {
        g_set_error(error_r, ssl_quark(), 0, "SSL_CTX_new() failed");
        return NULL;
    }

    ssl_factory *factory = new ssl_factory(ssl_ctx);

    if (!apply_config(ssl_ctx, config, error_r) ||
        !load_certs_keys(*factory, *config, error_r)) {
        delete factory;
        return NULL;
    }

    if (factory->cert_key.size() > 1 && !factory->EnableSNI(error_r)) {
        delete factory;
        return NULL;
    }

    return factory;
}

void
ssl_factory_free(struct ssl_factory *factory)
{
    delete factory;
}

SSL *
ssl_factory_make(struct ssl_factory *factory)
{
    return SSL_new(factory->ssl_ctx);
}
