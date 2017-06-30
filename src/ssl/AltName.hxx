/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_ALT_NAME_HXX
#define BENG_PROXY_SSL_ALT_NAME_HXX

#include "util/Compiler.h"

#include <openssl/ossl_typ.h>

#include <forward_list>
#include <string>

gcc_pure
std::forward_list<std::string>
GetSubjectAltNames(X509 &cert);

#endif
