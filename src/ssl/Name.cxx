/*
 * OpenSSL utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Name.hxx"
#include "MemBio.hxx"
#include "Unique.hxx"

AllocatedString<>
ToString(X509_NAME *name)
{
    if (name == nullptr)
        return nullptr;

    return BioWriterToString([name](BIO &bio){
            X509_NAME_print_ex(&bio, name, 0,
                               ASN1_STRFLGS_UTF8_CONVERT | XN_FLAG_SEP_COMMA_PLUS);
        });
}

AllocatedString<>
NidToString(X509_NAME &name, int nid)
{
    char buffer[1024];
    int len = X509_NAME_get_text_by_NID(&name, nid, buffer, sizeof(buffer));
    if (len < 0)
        return nullptr;

    return AllocatedString<>::Duplicate(buffer, len);
}
