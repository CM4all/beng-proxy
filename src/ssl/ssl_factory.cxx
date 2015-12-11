/*
 * SSL/TLS configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_factory.hxx"
#include "ssl_config.hxx"
#include "ssl_domain.hxx"
#include "Error.hxx"
#include "Unique.hxx"
#include "Name.hxx"
#include "Util.hxx"
#include "util/AllocatedString.hxx"

#include <inline/compiler.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <algorithm>

#include <assert.h>

struct SslFactoryCertKey {
    UniqueSSL_CTX ssl_ctx;

    AllocatedString<> common_name = nullptr;
    size_t cn_length;

    SslFactoryCertKey() = default;

    SslFactoryCertKey(SslFactoryCertKey &&other) = default;
    SslFactoryCertKey &operator=(SslFactoryCertKey &&other) = default;

    void LoadClient();

    void LoadServer(const SslConfig &parent_config,
                    const SslCertKeyConfig &config);

    void CacheCommonName(X509_NAME *subject) {
        common_name = NidToString(*subject, NID_commonName);
        if (common_name != nullptr)
            cn_length = strlen(common_name.c_str());
    }

    void CacheCommonName(X509 *cert) {
        assert(common_name == nullptr);

        X509_NAME *subject = X509_get_subject_name(cert);
        if (subject != nullptr)
            CacheCommonName(subject);
    }

    gcc_pure
    bool MatchCommonName(const char *host_name, size_t hn_length) const;

    UniqueSSL Make() const {
        UniqueSSL ssl(SSL_new(ssl_ctx.get()));
        if (!ssl)
            throw SslError("SSL_new() failed");

        return ssl;
    }

    void Apply(SSL *ssl) const {
        SSL_set_SSL_CTX(ssl, ssl_ctx.get());
    }

    unsigned Flush(long tm);
};

struct SslFactory {
    std::vector<SslFactoryCertKey> cert_key;

    const bool server;

    explicit SslFactory(bool _server)
        :server(_server) {}

    void EnableSNI();

    UniqueSSL Make();

    unsigned Flush(long tm);
};

static int
verify_callback(int ok, gcc_unused X509_STORE_CTX *ctx)
{
    return ok;
}

static void
load_certs_keys(SslFactory &factory, const SslConfig &config)
{
    factory.cert_key.reserve(config.cert_key.size());

    for (const auto &c : config.cert_key) {
        SslFactoryCertKey ck;
        ck.LoadServer(config, c);

        factory.cert_key.emplace_back(std::move(ck));
    }
}

static void
ApplyServerConfig(SSL_CTX *ssl_ctx, const SslCertKeyConfig &cert_key)
{
    ERR_clear_error();

    if (SSL_CTX_use_RSAPrivateKey_file(ssl_ctx,
                                       cert_key.key_file.c_str(),
                                       SSL_FILETYPE_PEM) != 1)
        throw SslError("Failed to load key file " +
                       cert_key.key_file);

    if (SSL_CTX_use_certificate_chain_file(ssl_ctx,
                                           cert_key.cert_file.c_str()) != 1)
        throw SslError("Failed to load certificate file " +
                       cert_key.cert_file);
}

static void
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

inline bool
SslFactoryCertKey::MatchCommonName(const char *host_name,
                                   size_t hn_length) const
{
    if (common_name == nullptr)
        return false;

    if (strcmp(host_name, common_name.c_str()) == 0)
        return true;

    if (common_name[0] == '*' && common_name[1] == '.' &&
        common_name[2] != 0) {
        if (hn_length >= cn_length &&
            /* match only one segment (no dots) */
            memchr(host_name, '.', hn_length - cn_length + 1) == nullptr &&
            memcmp(host_name + hn_length - cn_length + 1,
                   common_name.c_str() + 1, cn_length - 1) == 0)
            return true;
    }

    return false;
}

static int
ssl_servername_callback(SSL *ssl, gcc_unused int *al,
                        const SslFactory &factory)
{
    const char *host_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (host_name == nullptr)
        return SSL_TLSEXT_ERR_OK;

    const size_t length = strlen(host_name);

    /* find the first certificate that matches */

    for (const auto &ck : factory.cert_key) {
        if (ck.MatchCommonName(host_name, length)) {
            /* found it - now use it */
            ck.Apply(ssl);
            break;
        }
    }

    return SSL_TLSEXT_ERR_OK;
}

inline void
SslFactory::EnableSNI()
{
    SSL_CTX *ssl_ctx = cert_key.front().ssl_ctx.get();

    if (!SSL_CTX_set_tlsext_servername_callback(ssl_ctx,
                                                ssl_servername_callback) ||
        !SSL_CTX_set_tlsext_servername_arg(ssl_ctx, this))
        throw SslError("SSL_CTX_set_tlsext_servername_callback() failed");
}

inline UniqueSSL
SslFactory::Make()
{
    auto ssl = cert_key.front().Make();

    if (server)
        SSL_set_accept_state(ssl.get());
    else
        SSL_set_connect_state(ssl.get());

    return ssl;
}

inline unsigned
SslFactoryCertKey::Flush(long tm)
{
    unsigned before = SSL_CTX_sess_number(ssl_ctx.get());
    SSL_CTX_flush_sessions(ssl_ctx.get(), tm);
    unsigned after = SSL_CTX_sess_number(ssl_ctx.get());
    return after < before ? before - after : 0;
}

inline unsigned
SslFactory::Flush(long tm)
{
    unsigned n = 0;
    for (auto &i : cert_key)
        n += i.Flush(tm);
    return n;
}

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

static UniqueSSL_CTX
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

void
SslFactoryCertKey::LoadClient()
{
    assert(ssl_ctx == nullptr);

    ssl_ctx = CreateBasicSslCtx(false);
}

void
SslFactoryCertKey::LoadServer(const SslConfig &parent_config,
                              const SslCertKeyConfig &config)
{
    assert(ssl_ctx == nullptr);

    ssl_ctx = CreateBasicSslCtx(true);

    assert(!parent_config.cert_key.empty());

    ApplyServerConfig(ssl_ctx.get(), config);
    ApplyServerConfig(ssl_ctx.get(), parent_config);

    auto ssl = Make();

    X509 *cert = SSL_get_certificate(ssl.get());
    if (cert == nullptr)
        throw SslError("No certificate in SSL_CTX");

    EVP_PKEY *key = SSL_get_privatekey(ssl.get());
    if (key == nullptr)
        throw SslError("No certificate in SSL_CTX");

    if (!MatchModulus(*cert, *key))
        throw SslError("Key '" + config.key_file +
                       "' does not match certificate '" +
                       config.cert_file + "'");

    CacheCommonName(cert);
}

SslFactory *
ssl_factory_new_client()
{
    std::unique_ptr<SslFactory> factory(new SslFactory(false));

    factory->cert_key.emplace_back();
    factory->cert_key.front().LoadClient();

    return factory.release();
}

SslFactory *
ssl_factory_new_server(const SslConfig &config)
{
    assert(!config.cert_key.empty());

    std::unique_ptr<SslFactory> factory(new SslFactory(true));

    assert(!config.cert_key.empty());

    load_certs_keys(*factory, config);

    if (factory->cert_key.size() > 1)
        factory->EnableSNI();

    return factory.release();
}

void
ssl_factory_free(SslFactory *factory)
{
    delete factory;
}

UniqueSSL
ssl_factory_make(SslFactory &factory)
{
    return factory.Make();
}

unsigned
ssl_factory_flush(SslFactory &factory, long tm)
{
    return factory.Flush(tm);
}
