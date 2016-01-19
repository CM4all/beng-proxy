/*
 * OpenSSL file utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "LoadFile.hxx"
#include "Error.hxx"

#include <openssl/ts.h>
#include <openssl/err.h>

UniqueX509
LoadCertFile(const char *path)
{
    auto cert = TS_CONF_load_cert(path);
    if (cert == nullptr)
        throw SslError("Failed to load certificate");

    return UniqueX509(cert);
}

std::forward_list<UniqueX509>
LoadCertChainFile(const char *path)
{
    UniqueBIO bio(BIO_new_file(path, "r"));
    if (!bio)
        throw SslError(std::string("Failed to open ") + path);

    std::forward_list<UniqueX509> list;
    auto i = list.before_begin();

    UniqueX509 cert(PEM_read_bio_X509_AUX(bio.get(), nullptr,
                                          nullptr, nullptr));
    if (!cert)
        throw SslError(std::string("Failed to read certificate from ") + path);

    if (X509_check_ca(cert.get()) != 1)
        throw SslError(std::string("Not a CA certificate: ") + path);

    i = list.emplace_after(i, std::move(cert));

    while (true) {
        cert.reset(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
        if (!cert) {
            auto err = ERR_peek_last_error();
            if (ERR_GET_LIB(err) == ERR_LIB_PEM &&
                ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
                ERR_clear_error();
                break;
            }

            throw SslError(std::string("Failed to read certificate chain from ") + path);
        }

        if (X509_check_ca(cert.get()) != 1)
            throw SslError(std::string("Not a CA certificate: ") + path);

        EVP_PKEY *key = X509_get_pubkey(cert.get());
        if (key == nullptr)
            throw SslError(std::string("CA certificate has no pubkey in ") + path);

        if (X509_verify(i->get(), key) <= 0)
            throw SslError(std::string("CA chain mismatch in ") + path);

        i = list.emplace_after(i, std::move(cert));
    }

    return list;
}

UniqueEVP_PKEY
LoadKeyFile(const char *path)
{
    auto key = TS_CONF_load_key(path, nullptr);
    if (key == nullptr)
        throw SslError("Failed to load key");

    return UniqueEVP_PKEY(key);
}
