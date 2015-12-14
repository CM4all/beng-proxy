/*
 * SSL/TLS initialisation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_BASIC_HXX
#define BENG_PROXY_SSL_BASIC_HXX

#include "Unique.hxx"

UniqueSSL_CTX
CreateBasicSslCtx(bool server);

#endif
