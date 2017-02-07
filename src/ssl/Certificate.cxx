/*
 * OpenSSL certificate utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Certificate.hxx"
#include "Error.hxx"
#include "util/ConstBuffer.hxx"

#include <openssl/err.h>

UniqueX509
DecodeDerCertificate(ConstBuffer<void> der)
{
    ERR_clear_error();

    auto data = (const unsigned char *)der.data;
    UniqueX509 cert(d2i_X509(nullptr, &data, der.size));
    if (!cert)
        throw SslError("d2i_X509() failed");

    return cert;

}
