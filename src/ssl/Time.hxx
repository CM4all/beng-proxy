/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SSL_TIME_HXX
#define SSL_TIME_HXX

#include "util/AllocatedString.hxx"

#include <openssl/ossl_typ.h>

AllocatedString<>
FormatTime(ASN1_TIME &t);

AllocatedString<>
FormatTime(ASN1_TIME *t);

#endif
