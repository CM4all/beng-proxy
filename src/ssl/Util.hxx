/*
 * OpenSSL utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_UTIL_HXX
#define BENG_PROXY_SSL_UTIL_HXX

#include "Unique.hxx"

#include <inline/compiler.h>

#include <openssl/ossl_typ.h>

UniqueEVP_PKEY
GenerateRsaKey();

gcc_pure
bool
MatchModulus(const EVP_PKEY &key1, const EVP_PKEY &key2);

gcc_pure
bool
MatchModulus(X509 &cert, const EVP_PKEY &key);

#endif
