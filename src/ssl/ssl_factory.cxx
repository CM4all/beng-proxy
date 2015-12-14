/*
 * SSL/TLS configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_factory.hxx"
#include "ssl_config.hxx"
#include "ssl_domain.hxx"
#include "Error.hxx"
#include "Basic.hxx"
#include "Unique.hxx"
#include "Name.hxx"
#include "Util.hxx"
#include "util/AllocatedString.hxx"
#include "util/StringView.hxx"

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
    bool MatchCommonName(StringView host_name) const;

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

    void EnableSNI();

    UniqueSSL Make();

    unsigned Flush(long tm);
};

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

inline bool
SslFactoryCertKey::MatchCommonName(StringView host_name) const
{
    if (common_name == nullptr)
        return false;

    if (cn_length == host_name.size &&
        memcmp(host_name.data, common_name.c_str(), host_name.size) == 0)
        return true;

    if (common_name[0] == '*' && common_name[1] == '.' &&
        common_name[2] != 0) {
        if (host_name.size >= cn_length &&
            /* match only one segment (no dots) */
            memchr(host_name.data, '.',
                   host_name.size - cn_length + 1) == nullptr &&
            memcmp(host_name.data + host_name.size - cn_length + 1,
                   common_name.c_str() + 1, cn_length - 1) == 0)
            return true;
    }

    return false;
}

static int
ssl_servername_callback(SSL *ssl, gcc_unused int *al,
                        const SslFactory &factory)
{
    const char *_host_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (_host_name == nullptr)
        return SSL_TLSEXT_ERR_OK;

    const StringView host_name(_host_name);

    /* find the first certificate that matches */

    for (const auto &ck : factory.cert_key) {
        if (ck.MatchCommonName(host_name)) {
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

    SSL_set_accept_state(ssl.get());

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
ssl_factory_new_server(const SslConfig &config)
{
    assert(!config.cert_key.empty());

    std::unique_ptr<SslFactory> factory(new SslFactory());

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
