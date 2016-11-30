/*
 * OpenSSL key utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_KEY_HXX
#define BENG_PROXY_SSL_KEY_HXX

#include "Unique.hxx"

#include <inline/compiler.h>

#include <openssl/ossl_typ.h>

UniqueEVP_PKEY
GenerateRsaKey();

gcc_pure
bool
MatchModulus(EVP_PKEY &key1, EVP_PKEY &key2);

gcc_pure
bool
MatchModulus(X509 &cert, EVP_PKEY &key);

#endif
