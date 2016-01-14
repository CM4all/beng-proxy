/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "AltName.hxx"
#include "Unique.hxx"

static void
FillNameList(std::list<std::string> &list, GENERAL_NAMES &gn)
{
    for (int i = 0, n = sk_GENERAL_NAME_num(&gn); i < n; ++i) {
        const GENERAL_NAME *name = sk_GENERAL_NAME_value(&gn, i);
        if (name->type == GEN_DNS) {
            unsigned char *dns_name = ASN1_STRING_data(name->d.dNSName);
            if (dns_name == nullptr)
                continue;

            int dns_name_len = ASN1_STRING_length(name->d.dNSName);
            list.push_back(std::string(reinterpret_cast<const char *>(dns_name),
                                       dns_name_len));
        }
    }
}

gcc_pure
std::list<std::string>
GetSubjectAltNames(X509 &cert)
{
    std::list<std::string> list;

    for (int i = 0;
         (i = X509_get_ext_by_NID(&cert, NID_subject_alt_name, i)) >= 0;) {
        auto ext = X509_get_ext(&cert, i);
        if (ext == nullptr)
            continue;

        UniqueGENERAL_NAMES gn(reinterpret_cast<GENERAL_NAMES *>(X509V3_EXT_d2i(ext)));
        if (!gn)
            continue;

        FillNameList(list, *gn);
    }

    return list;
}
