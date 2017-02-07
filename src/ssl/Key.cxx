/*
 * OpenSSL key utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Key.hxx"
#include "Unique.hxx"
#include "Error.hxx"
#include "util/ConstBuffer.hxx"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/err.h>

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

UniqueEVP_PKEY
DecodeDerKey(ConstBuffer<void> der)
{
    ERR_clear_error();

    auto data = (const unsigned char *)der.data;
    UniqueEVP_PKEY key(d2i_AutoPrivateKey(nullptr, &data, der.size));
    if (!key)
        throw SslError("d2i_AutoPrivateKey() failed");

    return key;
}

static bool
MatchModulus(RSA &key1, RSA &key2)
{
    const BIGNUM *n1, *n2;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    RSA_get0_key(&key1, &n1, nullptr, nullptr);
    RSA_get0_key(&key2, &n2, nullptr, nullptr);
#else
    n1 = key1.n;
    n2 = key2.n;
#endif

    return BN_cmp(n1, n2) == 0;
}

static bool
MatchModulus(DSA &key1, DSA &key2)
{
    const BIGNUM *n1, *n2;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    DSA_get0_key(&key1, &n1, nullptr);
    DSA_get0_key(&key2, &n2, nullptr);
#else
    n1 = key1.pub_key;
    n2 = key2.pub_key;
#endif

    return BN_cmp(n1, n2) == 0;
}

/**
 * Are both public keys equal?
 */
bool
MatchModulus(EVP_PKEY &key1, EVP_PKEY &key2)
{
    if (EVP_PKEY_base_id(&key1) != EVP_PKEY_base_id(&key2))
        return false;

    switch (EVP_PKEY_base_id(&key1)) {
    case EVP_PKEY_RSA:
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        return MatchModulus(*EVP_PKEY_get0_RSA(&key1),
                            *EVP_PKEY_get0_RSA(&key2));
#else
        return MatchModulus(*key1.pkey.rsa, *key2.pkey.rsa);
#endif

    case EVP_PKEY_DSA:
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        return MatchModulus(*EVP_PKEY_get0_DSA(&key1),
                            *EVP_PKEY_get0_DSA(&key2));
#else
        return MatchModulus(*key1.pkey.dsa, *key2.pkey.dsa);
#endif

    default:
        return false;
    }
}

/**
 * Does the certificate belong to the given key?
 */
bool
MatchModulus(X509 &cert, EVP_PKEY &key)
{
    UniqueEVP_PKEY public_key(X509_get_pubkey(&cert));
    if (public_key == nullptr)
        return false;

    return MatchModulus(*public_key, key);
}
