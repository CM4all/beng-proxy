/*
 * OpenSSL utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Name.hxx"
#include "Unique.hxx"

AllocatedString<>
ToString(X509_NAME *name)
{
    if (name == nullptr)
        return nullptr;

    UniqueBIO bio(BIO_new(BIO_s_mem()));
    if (bio == nullptr)
        return nullptr;

    X509_NAME_print_ex(bio.get(), name, 0,
                       ASN1_STRFLGS_UTF8_CONVERT | XN_FLAG_SEP_COMMA_PLUS);
    char buffer[1024];
    int length = BIO_read(bio.get(), buffer, sizeof(buffer) - 1);

    return AllocatedString<>::Duplicate(buffer, length);
}
