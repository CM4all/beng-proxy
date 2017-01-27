/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_SESSION_CACHE_HXX
#define BENG_PROXY_SSL_SESSION_CACHE_HXX

#include <inline/compiler.h>

#include <openssl/ssl.h>

gcc_pure
static inline unsigned
GetSessionCacheNumber(SSL_CTX &ssl_ctx)
{
    return SSL_CTX_sess_number(&ssl_ctx);
}

/**
 * Flush expired sessions from the session cache.
 *
 * @return the number of expired sessions
 */
static inline unsigned
FlushSessionCache(SSL_CTX &ssl_ctx, long tm)
{
    const unsigned before = GetSessionCacheNumber(ssl_ctx);
    SSL_CTX_flush_sessions(&ssl_ctx, tm);
    const unsigned after = GetSessionCacheNumber(ssl_ctx);
    return after < before ? before - after : 0;
}

#endif
