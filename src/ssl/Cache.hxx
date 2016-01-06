/*
 * SSL/TLS certificate database and cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CERT_CACHE_HXX
#define BENG_PROXY_SSL_CERT_CACHE_HXX

#include "NameCache.hxx"
#include "Unique.hxx"
#include "certdb/Config.hxx"
#include "certdb/CertDatabase.hxx"
#include "stock/ThreadedStock.hxx"

#include <unordered_map>
#include <string>
#include <mutex>

struct CertDatabase;

/**
 * A frontend for #CertDatabase which caches results as SSL_CTX
 * instance.  It is thread-safe, designed to be called synchronously
 * by worker threads (via #SslFilter).
 */
class CertCache final : CertNameCacheHandler {
    const CertDatabaseConfig config;

    CertNameCache name_cache;

    /**
     * Database connections used by worker threads.
     */
    ThreadedStock<CertDatabase> dbs;

    std::mutex mutex;

    /**
     * Map host names to SSL_CTX instances.  The key may be a
     * wildcard.
     */
    std::unordered_map<std::string, std::shared_ptr<SSL_CTX>> map;

public:
    explicit CertCache(const CertDatabaseConfig &_config)
        :config(_config), name_cache(_config, *this) {}

    void Disconnect() {
        name_cache.Disconnect();
    }

    /**
     * Look up a certificate by host name.  Returns the SSL_CTX
     * pointer on success, nullptr if no matching certificate was
     * found, and throws an exception on error.
     */
    std::shared_ptr<SSL_CTX> Get(const char *host);

private:
    std::shared_ptr<SSL_CTX> Add(UniqueX509 &&cert, UniqueEVP_PKEY &&key);
    std::shared_ptr<SSL_CTX> Query(const char *host);

    /* virtual methods from class CertNameCacheHandler */
    void OnCertModified(const std::string &name, bool deleted) override;
};

#endif
