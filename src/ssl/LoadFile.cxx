/*
 * OpenSSL file utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "LoadFile.hxx"
#include "Error.hxx"

#include <openssl/ts.h>

UniqueX509
LoadCertFile(const char *path)
{
    auto cert = TS_CONF_load_cert(path);
    if (cert == nullptr)
        throw SslError("Failed to load certificate");

    return UniqueX509(cert);
}

UniqueEVP_PKEY
LoadKeyFile(const char *path)
{
    auto key = TS_CONF_load_key(path, nullptr);
    if (key == nullptr)
        throw SslError("Failed to load key");

    return UniqueEVP_PKEY(key);
}
