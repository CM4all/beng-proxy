// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "LookupCertResult.hxx"

#include <openssl/ossl_typ.h>

class SslCompletionHandler;
class CancellablePointer;

/**
 * C++ wrapper for the SSL_CTX_set_cert_cb() callback function.
 */
class SslCertCallback {
public:
	virtual ~SslCertCallback() noexcept = default;

	/**
	 * The actual certificate callback.  This method is supposed
	 * to look up the given host name and then call
	 * SSL_use_certificate() and SSL_use_PrivateKey().
	 *
	 * @param ssl a #SSL object which must have a
	 * #SslCompletionHandler (via SetSslCompletionHandler()); this
	 * handler will be invoked after this method has returned
	 * #IN_PROGRESS; using its #CancellablePointer field, the
	 * caller may cancel the operation
	 */
	virtual LookupCertResult OnCertCallback(SSL &ssl, const char *name) noexcept = 0;
};
