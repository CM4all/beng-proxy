// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Init.hxx"
#include "CompletionHandler.hxx"
#include "FifoBufferBio.hxx"

#include <openssl/ssl.h>

#if OPENSSL_VERSION_NUMBER < 0x30000000L
#include <openssl/engine.h>
#endif

void
ssl_global_init()
{
	OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS|
			 OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
			 nullptr);

#if OPENSSL_VERSION_NUMBER < 0x30000000L
	ENGINE_load_builtin_engines();
#endif

	InitSslCompletionHandler();
}

void
ssl_global_deinit() noexcept
{
	DeinitFifoBufferBio();
}
