/*
 * OpenSSL utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_UNIQUE_HXX
#define BENG_PROXY_SSL_UNIQUE_HXX

#include <openssl/ssl.h>
#include <openssl/bn.h>

#include <memory>

struct OpenSslDelete {
    void operator()(SSL *ssl) {
        SSL_free(ssl);
    }

    void operator()(X509 *x509) {
        X509_free(x509);
    }

    void operator()(X509_REQ *r) {
        X509_REQ_free(r);
    }

    void operator()(X509_NAME *name) {
        X509_NAME_free(name);
    }

    void operator()(X509_EXTENSION *ext) {
        X509_EXTENSION_free(ext);
    }

    void operator()(X509_EXTENSIONS *sk) {
        sk_X509_EXTENSION_pop_free(sk, X509_EXTENSION_free);
    }

    void operator()(RSA *rsa) {
        RSA_free(rsa);
    }

    void operator()(EC_KEY *key) {
        EC_KEY_free(key);
    }

    void operator()(EVP_PKEY *key) {
        EVP_PKEY_free(key);
    }

    void operator()(EVP_PKEY_CTX *key) {
        EVP_PKEY_CTX_free(key);
    }

    void operator()(BIO *bio) {
        BIO_free(bio);
    }

    void operator()(BIGNUM *bn) {
        BN_free(bn);
    }
};

using UniqueSSL = std::unique_ptr<SSL, OpenSslDelete>;
using UniqueX509 = std::unique_ptr<X509, OpenSslDelete>;
using UniqueX509_REQ = std::unique_ptr<X509_REQ, OpenSslDelete>;
using UniqueX509_NAME = std::unique_ptr<X509_NAME, OpenSslDelete>;
using UniqueX509_EXTENSION = std::unique_ptr<X509_EXTENSION, OpenSslDelete>;
using UniqueX509_EXTENSIONS = std::unique_ptr<X509_EXTENSIONS, OpenSslDelete>;
using UniqueRSA = std::unique_ptr<RSA, OpenSslDelete>;
using UniqueEC_KEY = std::unique_ptr<EC_KEY, OpenSslDelete>;
using UniqueEVP_PKEY = std::unique_ptr<EVP_PKEY, OpenSslDelete>;
using UniqueEVP_PKEY_CTX = std::unique_ptr<EVP_PKEY_CTX, OpenSslDelete>;
using UniqueBIO = std::unique_ptr<BIO, OpenSslDelete>;
using UniqueBIGNUM = std::unique_ptr<BIGNUM, OpenSslDelete>;

#endif
