/*
 * OpenSSL certificate utilities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CERTIFICATE_HXX
#define BENG_PROXY_SSL_CERTIFICATE_HXX

#include "Unique.hxx"

template<typename T> struct ConstBuffer;

/**
 * Decode an X.509 certificate encoded with DER.  It is a wrapper for
 * d2i_X509().
 *
 * Throws SslError on error.
 */
UniqueX509
DecodeDerCertificate(ConstBuffer<void> der);

#endif
