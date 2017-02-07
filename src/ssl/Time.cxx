/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Time.hxx"
#include "MemBio.hxx"

AllocatedString<>
FormatTime(ASN1_TIME &t)
{
    return BioWriterToString([&t](BIO &bio){
            ASN1_TIME_print(&bio, &t);
        });
}

AllocatedString<>
FormatTime(ASN1_TIME *t)
{
    if (t == nullptr)
        return nullptr;

    return FormatTime(*t);
}
