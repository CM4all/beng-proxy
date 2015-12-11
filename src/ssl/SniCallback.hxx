/*
 * SSL/TLS certificate database and cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_SNI_CALLBACK_HXX
#define BENG_PROXY_SSL_SNI_CALLBACK_HXX

#include <openssl/ossl_typ.h>

class SslSniCallback {
public:
    virtual ~SslSniCallback() {}

    virtual void OnSni(SSL *ssl, const char *name) = 0;
};

#endif
