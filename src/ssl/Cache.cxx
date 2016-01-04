/*
 * SSL/TLS certificate database and cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Cache.hxx"
#include "Basic.hxx"
#include "Util.hxx"
#include "Name.hxx"
#include "certdb/Wildcard.hxx"
#include "pg/Error.hxx"
#include "ssl/Error.hxx"
#include "util/AllocatedString.hxx"

#include <openssl/err.h>

static PgResult
CheckError(PgResult &&result)
{
    if (result.IsError())
        throw PgError(std::move(result));

    return std::move(result);
}

CertCache::~CertCache() {}

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

    if (SSL_CTX_use_PrivateKey(ssl_ctx.get(), key.release()) != 1)
        throw SslError("SSL_CTX_use_PrivateKey() failed");

    if (SSL_CTX_use_certificate(ssl_ctx.get(), cert.release()) != 1)
        throw SslError("SSL_CTX_use_certificate() failed");

    std::shared_ptr<SSL_CTX> shared(ssl_ctx.release());

    if (name != nullptr)
        map.emplace(name.c_str(), shared);

    return shared;
}

std::shared_ptr<SSL_CTX>
CertCache::Query(const char *host)
{
    auto db = dbs.Get(config);
    db->EnsureConnected();

    auto result = CheckError(db->FindServerCertificateKeyByCommonName(host));
    if (result.GetRowCount() < 1 ||
        result.IsValueNull(0, 0) || result.IsValueNull(0, 1))
        return std::shared_ptr<SSL_CTX>();

    const auto cert_der = result.GetBinaryValue(0, 0);
    const auto key_der = result.GetBinaryValue(0, 1);

    ERR_clear_error();

    auto cert_data = (const unsigned char *)cert_der.value;
    UniqueX509 cert(d2i_X509(nullptr, &cert_data, cert_der.size));
    if (!cert)
        throw SslError("d2i_X509() failed");

    auto key_data = (const unsigned char *)key_der.value;
    UniqueEVP_PKEY key(d2i_AutoPrivateKey(nullptr, &key_data, key_der.size));
    if (!key)
        throw SslError("d2i_PrivateKey() failed");

    if (!MatchModulus(*cert, *key))
        throw SslError(std::string("Key does not match certificate for '")
                       + host + "'");

    return Add(std::move(cert), std::move(key));
}

std::shared_ptr<SSL_CTX>
CertCache::Get(const char *host)
{
    const std::unique_lock<std::mutex> lock(mutex);

    auto i = map.find(host);
    if (i != map.end())
        return i->second;

    const auto wildcard = MakeCommonNameWildcard(host);
    if (!wildcard.empty()) {
        i = map.find(wildcard);
        if (i != map.end())
            return i->second;
    }

    auto ssl_ctx = Query(host);
    if (!ssl_ctx && !wildcard.empty())
        /* not found: try the wildcard */
        ssl_ctx = Query(wildcard.c_str());

    return ssl_ctx;
}
