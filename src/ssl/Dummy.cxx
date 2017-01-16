/*
 * OpenSSL utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Dummy.hxx"
#include "Edit.hxx"
#include "Error.hxx"

UniqueX509
MakeSelfIssuedDummyCert(const char *common_name)
{
    UniqueX509 cert(X509_new());
    if (cert == nullptr)
        throw SslError("X509_new() failed");

    auto *name = X509_get_subject_name(cert.get());

    if (!X509_NAME_add_entry_by_NID(name, NID_commonName, MBSTRING_ASC,
                                    const_cast<unsigned char *>((const unsigned char *)common_name),
                                    -1, -1, 0))
        throw SslError("X509_NAME_add_entry_by_NID() failed");

    X509_set_issuer_name(cert.get(), name);

    X509_set_version(cert.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
    X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(cert.get()), 60 * 60);

    AddExt(*cert, NID_basic_constraints, "critical,CA:TRUE");
    AddExt(*cert, NID_key_usage, "critical,keyCertSign");

    return cert;
}

UniqueX509
MakeSelfSignedDummyCert(EVP_PKEY &key, const char *common_name)
{
    auto cert = MakeSelfIssuedDummyCert(common_name);
    X509_set_pubkey(cert.get(), &key);
    if (!X509_sign(cert.get(), &key, EVP_sha1()))
        throw SslError("X509_sign() failed");

    return cert;
}
