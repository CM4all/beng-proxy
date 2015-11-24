/*
 * OpenSSL utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_NAME_HXX
#define BENG_PROXY_SSL_NAME_HXX

#include "util/AllocatedString.hxx"

#include <openssl/ossl_typ.h>

AllocatedString<>
ToString(X509_NAME *name);

#endif
