/*
 * SSL/TLS certificate database and cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_DB_SNI_CALLBACK_HXX
#define BENG_PROXY_SSL_DB_SNI_CALLBACK_HXX

#include "SniCallback.hxx"

class CertCache;

class DbSslSniCallback final : public SslSniCallback {
    CertCache &cache;

public:
    explicit DbSslSniCallback(CertCache &_cache):cache(_cache) {}

    void OnSni(SSL *ssl, const char *name) override;
};

#endif
