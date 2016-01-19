/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Buffer.hxx"
#include "Error.hxx"

SslBuffer::SslBuffer(X509 &cert)
{
    data = nullptr;
    int result = i2d_X509(&cert, &data);
    if (result < 0)
        throw SslError("Failed to encode certificate");

    size = result;
}

SslBuffer::SslBuffer(X509_NAME &name)
{
    data = nullptr;
    int result = i2d_X509_NAME(&name, &data);
    if (result < 0)
        throw SslError("Failed to encode name");

    size = result;
}

SslBuffer::SslBuffer(EVP_PKEY &key)
{
    data = nullptr;
    int result = i2d_PrivateKey(&key, &data);
    if (result < 0)
        throw SslError("Failed to encode key");

    size = result;
}
