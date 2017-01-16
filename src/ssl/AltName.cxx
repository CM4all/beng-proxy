/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "AltName.hxx"
#include "GeneralName.hxx"

static void
FillNameList(std::forward_list<std::string> &list, OpenSSL::GeneralNames src)
{
    for (const auto &name : src) {
        if (name.GetType() == GEN_DNS) {
            const auto dns_name = name.GetDnsName();
            if (dns_name.IsNull())
                continue;

            list.emplace_front(dns_name.data, dns_name.size);
        }
    }
}

gcc_pure
std::forward_list<std::string>
GetSubjectAltNames(X509 &cert)
{
    std::forward_list<std::string> list;

    for (int i = -1;
         (i = X509_get_ext_by_NID(&cert, NID_subject_alt_name, i)) >= 0;) {
        auto ext = X509_get_ext(&cert, i);
        if (ext == nullptr)
            continue;

        OpenSSL::UniqueGeneralNames gn(reinterpret_cast<GENERAL_NAMES *>(X509V3_EXT_d2i(ext)));
        if (!gn)
            continue;

        FillNameList(list, gn);
    }

    return list;
}
