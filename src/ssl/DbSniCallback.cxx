/*
 * SSL/TLS certificate database and cache.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "DbSniCallback.hxx"
#include "Cache.hxx"

void
DbSslSniCallback::OnSni(SSL *ssl, const char *name)
{
    auto ssl_ctx = cache.Get(name);
    if (ssl_ctx)
        SSL_set_SSL_CTX(ssl, ssl_ctx.get());
}
