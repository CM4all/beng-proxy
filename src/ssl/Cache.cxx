/*
 * SSL/TLS certificate database and cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Cache.hxx"
#include "Basic.hxx"
#include "Util.hxx"
#include "Name.hxx"
#include "Error.hxx"
#include "LoadFile.hxx"
#include "certdb/Wildcard.hxx"
#include "pg/Error.hxx"
#include "util/AllocatedString.hxx"

#include <daemon/log.h>

#include <openssl/err.h>

void
CertCache::LoadCaCertificate(const char *path)
{
    auto chain = LoadCertChainFile(path);
    assert(!chain.empty());

    X509_NAME *subject = X509_get_subject_name(chain.front().get());
    if (subject == nullptr)
        throw SslError(std::string("CA certificate has no subject: ") + path);

    auto digest = CalcSHA1(*subject);
    auto r = ca_certs.emplace(std::move(digest), std::move(chain));
    if (!r.second)
        throw SslError(std::string("Duplicate CA certificate: ") + path);
}

static PgResult
CheckError(PgResult &&result)
{
    if (result.IsError())
        throw PgError(std::move(result));

    return std::move(result);
}

gcc_pure
static AllocatedString<>
GetCommonName(X509_NAME &name)
{
    return NidToString(name, NID_commonName);
}

gcc_pure
static AllocatedString<>
GetCommonName(X509 *cert)
{
    X509_NAME *subject = X509_get_subject_name(cert);
    return subject != nullptr
        ? GetCommonName(*subject)
        : nullptr;
}

std::shared_ptr<SSL_CTX>
CertCache::Add(UniqueX509 &&cert, UniqueEVP_PKEY &&key)
{
    assert(cert);
    assert(key);

    auto ssl_ctx = CreateBasicSslCtx(true);
    // TODO: call ApplyServerConfig()

    ERR_clear_error();

    const auto name = GetCommonName(cert.get());

    X509_NAME *issuer = X509_get_issuer_name(cert.get());

    if (SSL_CTX_use_PrivateKey(ssl_ctx.get(), key.release()) != 1)
        throw SslError("SSL_CTX_use_PrivateKey() failed");

    if (SSL_CTX_use_certificate(ssl_ctx.get(), cert.release()) != 1)
        throw SslError("SSL_CTX_use_certificate() failed");

    if (issuer != nullptr) {
        auto i = ca_certs.find(CalcSHA1(*issuer));
        if (i != ca_certs.end())
            for (const auto &ca_cert : i->second)
                SSL_CTX_add_extra_chain_cert(ssl_ctx.get(),
                                             X509_dup(ca_cert.get()));
    }

    std::shared_ptr<SSL_CTX> shared(std::move(ssl_ctx));

    if (name != nullptr) {
        const std::unique_lock<std::mutex> lock(mutex);
        map.emplace(name.c_str(), shared);
    }

    return shared;
}

std::shared_ptr<SSL_CTX>
CertCache::Query(const char *host)
{
    auto db = dbs.Get(config);
    db->EnsureConnected();

    auto result = CheckError(db->FindServerCertificateKeyByName(host));
    if (result.GetRowCount() < 1 ||
        result.IsValueNull(0, 0) || result.IsValueNull(0, 1))
        return std::shared_ptr<SSL_CTX>();

    const auto cert_der = result.GetBinaryValue(0, 0);
    const auto key_der = result.GetBinaryValue(0, 1);

    ERR_clear_error();

    auto cert_data = (const unsigned char *)cert_der.data;
    UniqueX509 cert(d2i_X509(nullptr, &cert_data, cert_der.size));
    if (!cert)
        throw SslError("d2i_X509() failed");

    auto key_data = (const unsigned char *)key_der.data;
    UniqueEVP_PKEY key(d2i_AutoPrivateKey(nullptr, &key_data, key_der.size));
    if (!key)
        throw SslError("d2i_AutoPrivateKey() failed");

    if (!MatchModulus(*cert, *key))
        throw SslError(std::string("Key does not match certificate for '")
                       + host + "'");

    return Add(std::move(cert), std::move(key));
}

std::shared_ptr<SSL_CTX>
CertCache::GetNoWildCard(const char *host)
{
    {
        const std::unique_lock<std::mutex> lock(mutex);
        auto i = map.find(host);
        if (i != map.end())
            return i->second;
    }

    if (name_cache.Lookup(host)) {
        auto ssl_ctx = Query(host);
        if (ssl_ctx)
            return ssl_ctx;
    }

    return {};
}

std::shared_ptr<SSL_CTX>
CertCache::Get(const char *host)
{
    auto ssl_ctx = GetNoWildCard(host);
    if (!ssl_ctx) {
        /* not found: try the wildcard */
        const auto wildcard = MakeCommonNameWildcard(host);
        if (!wildcard.empty())
            ssl_ctx = GetNoWildCard(wildcard.c_str());
    }

    return ssl_ctx;
}

void
CertCache::OnCertModified(const std::string &name, bool deleted)
{
    const std::unique_lock<std::mutex> lock(mutex);
    auto i = map.find(name);
    if (i != map.end()) {
        map.erase(i);

        daemon_log(5, "flushed %s certificate '%s'\n",
                   deleted ? "deleted" : "modified",
                   name.c_str());
    }
}
