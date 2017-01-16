/*
 * Edit X.509 certificates and requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_EDIT_HXX
#define BENG_PROXY_SSL_EDIT_HXX

#include "openssl/x509.h"

namespace OpenSSL {
class GeneralNames;
}

void
AddExt(X509 &cert, int nid, const char *value);

void
AddAltNames(X509_REQ &req, OpenSSL::GeneralNames gn);

#endif
