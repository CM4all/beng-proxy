/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "LookupCertResult.hxx"

#include <openssl/ossl_typ.h>

class SslCompletionHandler;
class CancellablePointer;

class SslCertCompletionCallback {
public:
	/**
	 * The certificate lookup has completed.
	 *
	 * This method will be invoked in the main thread.
	 *
	 * @param true on success if a certificate was found, false if
	 * no certificate was found or an error has occurred
	 */
	virtual void OnCertCallbackComplete(SSL &ssl, bool found) noexcept = 0;
};

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
	 * May throw on error.
	 *
	 * @param ssl a #SSL object which must have a
	 * #SslCompletionHandler (via SetSslCompletionHandler()); this
	 * handler will be invoked after this method has returned
	 * #IN_PROGRESS; using its #CancellablePointer field, the
	 * caller may cancel the operation
	 */
	virtual LookupCertResult OnCertCallback(SSL &ssl, const char *name) = 0;
};
