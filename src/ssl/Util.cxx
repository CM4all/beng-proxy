/*
 * OpenSSL utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Util.hxx"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <assert.h>

/**
 * Are both public keys equal?
 */
bool
MatchModulus(const EVP_PKEY *key1, const EVP_PKEY *key2)
{
    assert(key1 != nullptr);
    assert(key2 != nullptr);

    if (key1->type != key2->type)
        return false;

    switch (key1->type) {
    case EVP_PKEY_RSA:
        return BN_cmp(key1->pkey.rsa->n, key2->pkey.rsa->n) == 0;

    case EVP_PKEY_DSA:
        return BN_cmp(key1->pkey.dsa->pub_key, key2->pkey.dsa->pub_key) == 0;

    default:
        return false;
    }
}

/**
 * Does the certificate belong to the given key?
 */
bool
MatchModulus(X509 *cert, const EVP_PKEY *key)
{
    assert(cert != nullptr);
    assert(key != nullptr);

    EVP_PKEY *public_key = X509_get_pubkey(cert);
    if (public_key == nullptr)
        return false;

    const bool result = MatchModulus(public_key, key);
    EVP_PKEY_free(public_key);
    return result;
}
