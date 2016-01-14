/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_ALT_NAME_HXX
#define BENG_PROXY_SSL_ALT_NAME_HXX

#include <inline/compiler.h>

#include <openssl/ossl_typ.h>

#include <list>
#include <string>

gcc_pure
std::list<std::string>
GetSubjectAltNames(X509 &cert);

#endif
