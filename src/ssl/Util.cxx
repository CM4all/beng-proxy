/*
 * OpenSSL utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Util.hxx"
#include "Unique.hxx"
#include "Error.hxx"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <assert.h>

UniqueEVP_PKEY
GenerateRsaKey()
{
    const UniqueBIGNUM e(BN_new());
    if (!e)
        throw SslError("BN_new() failed");

    if (!BN_set_word(e.get(), RSA_F4))
        throw SslError("BN_set_word() failed");

    UniqueRSA rsa(RSA_new());
    if (!rsa)
        throw SslError("RSA_new() failed");

    if (!RSA_generate_key_ex(rsa.get(), 4096, e.get(), nullptr))
        throw SslError("RSA_generate_key_ex() failed");

    UniqueEVP_PKEY key(EVP_PKEY_new());
    if (!key)
        throw SslError("EVP_PKEY_new() failed");

    if (!EVP_PKEY_assign_RSA(key.get(), rsa.get()))
        throw SslError("EVP_PKEY_assign_RSA() failed");

    rsa.release();

    return key;
}

/**
 * Are both public keys equal?
 */
bool
MatchModulus(const EVP_PKEY &key1, const EVP_PKEY &key2)
{
    if (key1.type != key2.type)
        return false;

    switch (key1.type) {
    case EVP_PKEY_RSA:
        return BN_cmp(key1.pkey.rsa->n, key2.pkey.rsa->n) == 0;

    case EVP_PKEY_DSA:
        return BN_cmp(key1.pkey.dsa->pub_key, key2.pkey.dsa->pub_key) == 0;

    default:
        return false;
    }
}

/**
 * Does the certificate belong to the given key?
 */
bool
MatchModulus(X509 &cert, const EVP_PKEY &key)
{
    UniqueEVP_PKEY public_key(X509_get_pubkey(&cert));
    if (public_key == nullptr)
        return false;

    return MatchModulus(*public_key, key);
}
