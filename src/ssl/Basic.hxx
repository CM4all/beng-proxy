/*
 * SSL/TLS initialisation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_BASIC_HXX
#define BENG_PROXY_SSL_BASIC_HXX

#include "Unique.hxx"

struct SslConfig;
class SslCtx;

SslCtx
CreateBasicSslCtx(bool server);

void
ApplyServerConfig(SSL_CTX &ssl_ctx, const SslConfig &config);

#endif
