/*
 * SSL/TLS initialisation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_FACTORY_H
#define BENG_PROXY_SSL_FACTORY_H

#include "Unique.hxx"

struct pool;
struct SslConfig;
struct SslFactory;

SslFactory *
ssl_factory_new(const SslConfig &config,
                bool server);

void
ssl_factory_free(SslFactory *factory);

UniqueSSL
ssl_factory_make(SslFactory &factory);

/**
 * Flush expired sessions from the session cache.
 *
 * @return the number of expired sessions
 */
unsigned
ssl_factory_flush(SslFactory &factory, long tm);

#endif
