/*
 * SSL/TLS certificate database and cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CERT_CACHE_HXX
#define BENG_PROXY_SSL_CERT_CACHE_HXX

#include "NameCache.hxx"
#include "Hash.hxx"
#include "Unique.hxx"
#include "certdb/Config.hxx"
#include "certdb/CertDatabase.hxx"
#include "stock/ThreadedStock.hxx"

#include <inline/compiler.h>

#include <unordered_map>
#include <map>
#include <forward_list>
#include <string>
#include <mutex>

#include <string.h>

class CertDatabase;

/**
 * A frontend for #CertDatabase which caches results as SSL_CTX
 * instance.  It is thread-safe, designed to be called synchronously
 * by worker threads (via #SslFilter).
 */
class CertCache final : CertNameCacheHandler {
    const CertDatabaseConfig config;

    CertNameCache name_cache;

    struct SHA1Compare {
        gcc_pure
        bool operator()(const SHA1Digest &a, const SHA1Digest &b) {
            return memcmp(&a, &b, sizeof(a)) < 0;
        }
    };

    std::map<SHA1Digest, std::forward_list<UniqueX509>, SHA1Compare> ca_certs;

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
    explicit CertCache(EventLoop &event_loop,
                       const CertDatabaseConfig &_config)
        :config(_config), name_cache(event_loop, _config, *this) {}

    void LoadCaCertificate(const char *path);

    void Connect() {
        name_cache.Connect();
    }

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
    std::shared_ptr<SSL_CTX> GetNoWildCard(const char *host);

    /* virtual methods from class CertNameCacheHandler */
    void OnCertModified(const std::string &name, bool deleted) override;
};

#endif
