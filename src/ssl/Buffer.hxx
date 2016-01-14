/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_BUFFER_HXX
#define BENG_PROXY_SSL_BUFFER_HXX

#include "util/WritableBuffer.hxx"
#include "util/ConstBuffer.hxx"

#include <openssl/ssl.h>

class SslBuffer : WritableBuffer<unsigned char> {
public:
    explicit SslBuffer(X509 *cert);
    explicit SslBuffer(EVP_PKEY *key);

    SslBuffer(SslBuffer &&src):WritableBuffer<unsigned char>(src) {
        src.data = nullptr;
    }

    ~SslBuffer() {
        if (data != nullptr)
            OPENSSL_free(data);
    }

    ConstBuffer<void> get() const {
        return {data, size};
    }
};

#endif
