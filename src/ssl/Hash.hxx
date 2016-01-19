/*
 * Calculate hashes of OpenSSL objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_HASH_HXX
#define BENG_PROXY_SSL_HASH_HXX

#include <inline/compiler.h>

#include <openssl/ossl_typ.h>
#include <openssl/sha.h>

template<typename T> struct ConstBuffer;

struct SHA1Digest {
    unsigned char data[SHA_DIGEST_LENGTH];
};

gcc_pure
SHA1Digest
CalcSHA1(ConstBuffer<void> src);

gcc_pure
SHA1Digest
CalcSHA1(X509_NAME &src);

#endif
