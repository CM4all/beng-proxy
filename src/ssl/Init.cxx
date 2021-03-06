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

#include "Init.hxx"
#include "FifoBufferBio.hxx"

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

#if OPENSSL_VERSION_NUMBER < 0x30000000L
#include <openssl/engine.h>
#endif

void
ssl_global_init()
{
	SSL_load_error_strings();
	SSL_library_init();

#if OPENSSL_VERSION_NUMBER < 0x30000000L
	ENGINE_load_builtin_engines();
#endif
}

void
ssl_global_deinit()
{
	DeinitFifoBufferBio();

#if OPENSSL_VERSION_NUMBER < 0x30000000L
	ENGINE_cleanup();
#endif

	EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();

	CRYPTO_set_id_callback(nullptr);
	CRYPTO_set_locking_callback(nullptr);

	ERR_free_strings();
}

void
ssl_thread_deinit()
{
}
