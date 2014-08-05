/*
 * SSL/TLS configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_factory.hxx"
#include "ssl_config.hxx"
#include "ssl_domain.hxx"
#include "util/Error.hxx"

#include <inline/compiler.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <assert.h>

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

    ssl_cert_key &operator=(ssl_cert_key &&other) {
        std::swap(cert, other.cert);
        std::swap(key, other.key);
        return *this;
    }

    bool Load(const ssl_cert_key_config &config, Error &error);
};

struct ssl_factory {
    SSL_CTX *const ssl_ctx;

    std::vector<ssl_cert_key> cert_key;

    const bool server;

    ssl_factory(SSL_CTX *_ssl_ctx, bool _server)
        :ssl_ctx(_ssl_ctx), server(_server) {}

    ~ssl_factory() {
        SSL_CTX_free(ssl_ctx);
    }

    bool EnableSNI(Error &error);

    SSL *Make();

    unsigned Flush(long tm);
};

static int
verify_callback(int ok, gcc_unused X509_STORE_CTX *ctx)
{
    return ok;
}

static BIO *
bio_open_file(const char *path, Error &error)
{
    BIO *bio = BIO_new_file(path, "r");
    if (bio == NULL)
        error.Format(ssl_domain, "Failed to open file %s", path);

    return bio;
}

static EVP_PKEY *
read_key_file(const char *path, Error &error)
{
    BIO *bio = bio_open_file(path, error);
    if (bio == NULL)
        return NULL;

    EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (key == NULL)
        error.Format(ssl_domain, "Failed to load key file %s", path);

    return key;
}

static X509 *
read_cert_file(const char *path, Error &error)
{
    BIO *bio = bio_open_file(path, error);
    if (bio == NULL)
        return NULL;

    X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (cert == NULL)
        error.Format(ssl_domain, "Failed to load certificate file %s", path);

    return cert;
}

/**
 * Are both public keys equal?
 */
gcc_pure
static bool
MatchModulus(EVP_PKEY *key1, EVP_PKEY *key2)
{
    assert(key1 != nullptr);
    assert(key2 != nullptr);

    if (key1->type != key2->type)
        return false;

    switch (key1->type) {
    case EVP_PKEY_RSA:
        return BN_cmp(key1->pkey.rsa->n, key2->pkey.rsa->n) == 0;

    case EVP_PKEY_DSA:
        return BN_cmp(key1->pkey.dsa->pub_key, key2->pkey.dsa->pub_key) == 0;

    default:
        return false;
    }
}

/**
 * Does the certificate belong to the given key?
 */
gcc_pure
static bool
MatchModulus(X509 *cert, EVP_PKEY *key)
{
    assert(cert != nullptr);
    assert(key != nullptr);

    EVP_PKEY *public_key = X509_get_pubkey(cert);
    if (public_key == nullptr)
        return false;

    const bool result = MatchModulus(public_key, key);
    EVP_PKEY_free(public_key);
    return result;
}

bool
ssl_cert_key::Load(const ssl_cert_key_config &config, Error &error)
{
    assert(key == nullptr);
    assert(cert == nullptr);

    key = read_key_file(config.key_file.c_str(), error);
    if (key == nullptr)
        return false;

    cert = read_cert_file(config.cert_file.c_str(), error);
    if (cert == nullptr)
        return false;

    if (!MatchModulus(cert, key)) {
        error.Format(ssl_domain, "Key '%s' does not match certificate '%s'",
                    config.key_file.c_str(), config.cert_file.c_str());
        return false;
    }

    return true;
}

static bool
load_certs_keys(ssl_factory &factory, const ssl_config &config,
                Error &error)
{
    factory.cert_key.reserve(config.cert_key.size());

    for (const auto &c : config.cert_key) {
        ssl_cert_key ck;
        if (!ck.Load(c, error))
            return false;

        factory.cert_key.emplace_back(std::move(ck));
    }

    return true;
}

static bool
apply_server_config(SSL_CTX *ssl_ctx, const ssl_config &config,
                    Error &error)
{
    assert(!config.cert_key.empty());

    ERR_clear_error();

    if (SSL_CTX_use_RSAPrivateKey_file(ssl_ctx,
                                       config.cert_key[0].key_file.c_str(),
                                       SSL_FILETYPE_PEM) != 1) {
        ERR_print_errors_fp(stderr);
        error.Format(ssl_domain, "Failed to load key file %s",
                     config.cert_key[0].key_file.c_str());
        return false;
    }

    if (SSL_CTX_use_certificate_chain_file(ssl_ctx,
                                           config.cert_key[0].cert_file.c_str()) != 1) {
        ERR_print_errors_fp(stderr);
        error.Format(ssl_domain, "Failed to load certificate file %s",
                     config.cert_key[0].cert_file.c_str());
        return false;
    }

    if (!config.ca_cert_file.empty()) {
        if (SSL_CTX_load_verify_locations(ssl_ctx,
                                          config.ca_cert_file.c_str(),
                                          NULL) != 1) {
            error.Format(ssl_domain, "Failed to load CA certificate file %s",
                         config.ca_cert_file.c_str());
            return false;
        }

        /* send all certificates from this file to the client (list of
           acceptable CA certificates) */

        STACK_OF(X509_NAME) *list =
            SSL_load_client_CA_file(config.ca_cert_file.c_str());
        if (list == NULL) {
            error.Format(ssl_domain,
                         "Failed to load CA certificate list from file %s",
                         config.ca_cert_file.c_str());
            return false;
        }

        SSL_CTX_set_client_CA_list(ssl_ctx, list);
    }

    if (config.verify != ssl_verify::NO) {
        /* enable client certificates */
        int mode = SSL_VERIFY_PEER;

        if (config.verify == ssl_verify::YES)
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
        if (hn_length >= cn_length &&
            /* match only one segment (no dots) */
            memchr(host_name, '.', hn_length - cn_length + 1) == NULL &&
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
ssl_factory::EnableSNI(Error &error)
{
    if (!SSL_CTX_set_tlsext_servername_callback(ssl_ctx,
                                                ssl_servername_callback) ||
        !SSL_CTX_set_tlsext_servername_arg(ssl_ctx, this)) {
        error.Format(ssl_domain,
                     "SSL_CTX_set_tlsext_servername_callback() failed");
        return false;
    }

    return true;
}

inline SSL *
ssl_factory::Make()
{
    SSL *ssl = SSL_new(ssl_ctx);
    if (ssl == nullptr)
        return nullptr;

    if (server)
        SSL_set_accept_state(ssl);
    else
        SSL_set_connect_state(ssl);

    return ssl;
}

inline unsigned
ssl_factory::Flush(long tm)
{
    unsigned before = SSL_CTX_sess_number(ssl_ctx);
    SSL_CTX_flush_sessions(ssl_ctx, tm);
    unsigned after = SSL_CTX_sess_number(ssl_ctx);
    return after < before ? before - after : 0;
}

/**
 * Enable Elliptic curve Diffie-Hellman (ECDH) for perfect forward
 * secrecy.  By default, it OpenSSL disables it.
 */
static bool
enable_ecdh(SSL_CTX *ssl_ctx, Error &error)
{
    /* OpenSSL 1.0.2 will allow this instead:

       SSL_CTX_set_ecdh_auto(ssl_ctx, 1)
    */

    EC_KEY *ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (ecdh == nullptr) {
        error.Set(ssl_domain, "EC_KEY_new_by_curve_name() failed");
        return nullptr;
    }

    bool success = SSL_CTX_set_tmp_ecdh(ssl_ctx, ecdh) == 1;
    EC_KEY_free(ecdh);
    if (!success)
        error.Set(ssl_domain, "SSL_CTX_set_tmp_ecdh() failed");

    return success;
}

struct ssl_factory *
ssl_factory_new(const ssl_config &config,
                bool server,
                Error &error)
{
    assert(!config.cert_key.empty() || !server);

    auto method = server
        ? SSLv23_server_method()
        : TLSv1_client_method();

    SSL_CTX *ssl_ctx = SSL_CTX_new(method);
    if (ssl_ctx == NULL) {
        error.Format(ssl_domain, "SSL_CTX_new() failed");
        return NULL;
    }

    if (server && !enable_ecdh(ssl_ctx, error)) {
        SSL_CTX_free(ssl_ctx);
        return nullptr;
    }

    ssl_factory *factory = new ssl_factory(ssl_ctx, server);

    if (server) {
        if (!apply_server_config(ssl_ctx, config, error) ||
            !load_certs_keys(*factory, config, error)) {
            delete factory;
            return NULL;
        }
    } else {
        assert(config.cert_key.empty());
        assert(config.ca_cert_file.empty());
        assert(config.verify == ssl_verify::NO);
    }

    if (factory->cert_key.size() > 1 && !factory->EnableSNI(error)) {
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
ssl_factory_make(ssl_factory &factory)
{
    return factory.Make();
}

unsigned
ssl_factory_flush(struct ssl_factory &factory, long tm)
{
    return factory.Flush(tm);
}
