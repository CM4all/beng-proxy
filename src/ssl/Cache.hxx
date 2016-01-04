/*
 * SSL/TLS certificate database and cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CERT_CACHE_HXX
#define BENG_PROXY_SSL_CERT_CACHE_HXX

#include "Unique.hxx"
#include "certdb/CertDatabase.hxx"

#include <unordered_map>
#include <string>
#include <mutex>

struct CertDatabaseConfig;

/**
 * A frontend for #CertDatabase which caches results as SSL_CTX
 * instance.  It is thread-safe, designed to be called synchronously
 * by worker threads (via #SslFilter).
 */
class CertCache {
    CertDatabase db;

    std::mutex mutex;

    std::unordered_map<std::string, std::shared_ptr<SSL_CTX>> map;

public:
    explicit CertCache(const CertDatabaseConfig &_config):db(_config) {}

    std::shared_ptr<SSL_CTX> Get(const char *host);

private:
    std::shared_ptr<SSL_CTX> Add(UniqueX509 &&cert, UniqueEVP_PKEY &&key);
    std::shared_ptr<SSL_CTX> Query(const char *host);
};

#endif
