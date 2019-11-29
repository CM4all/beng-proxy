/*
 * Copyright 2007-2017 Content Management AG
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

#include "util/Compiler.h"

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/engine.h>
#include <openssl/err.h>

#include <mutex>

#include <pthread.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L

static std::mutex *ssl_mutexes;

static void
locking_function(int mode, int n,
		 gcc_unused const char *file, gcc_unused int line)
{
	if (mode & CRYPTO_LOCK)
		ssl_mutexes[n].lock();
	else
		ssl_mutexes[n].unlock();
}

static unsigned long
id_function()
{
	return pthread_self();
}

#endif

void
ssl_global_init()
{
	SSL_load_error_strings();
	SSL_library_init();
	ENGINE_load_builtin_engines();

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	/* initialise OpenSSL multi-threading; this is needed because the
	   SSL_CTX object is shared among all threads, which need to
	   modify it in a safe manner */

	ssl_mutexes = new std::mutex[CRYPTO_num_locks()];

	CRYPTO_set_locking_callback(locking_function);
	CRYPTO_set_id_callback(id_function);
#endif
}

void
ssl_global_deinit()
{
	DeinitFifoBufferBio();

	ENGINE_cleanup();
	EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();

	CRYPTO_set_id_callback(nullptr);
	CRYPTO_set_locking_callback(nullptr);

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	delete[] ssl_mutexes;
#endif

	ERR_free_strings();

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	ERR_remove_thread_state(nullptr);
#endif
}

void
ssl_thread_deinit()
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	ERR_remove_thread_state(nullptr);
#endif
}
